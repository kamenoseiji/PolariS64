//	cuda_fft_xspec.c : FFT using CuFFT
//
//	Author : Seiji Kameno
//	Created: 2012/12/6
//
#include <cuda.h>
#include <cufft.h>
#include <string.h>
#include <math.h>
// #include </usr/local/cuda/samples/common/inc/timer.h>
// #include <cuda_runtime.h>
// #include <timer.h>
#include "cuda_polaris.inc"
#define SCALEFACT 1.0/(NFFT* NsegSec)

int	segment_offset(struct SHM_PARAM	*, int *);
int	fileRecOpen(struct SHM_PARAM	*, char *, FILE **, FILE **, FILE **);

main(
	int		argc,			// Number of Arguments
	char	**argv )		// Pointer to Arguments
{
	int		shrd_param_id;				// Shared Memory ID
	int		index;						// General Index
	int		threadID;					// Index for thread (= IF stream)
	int		seg_index;					// Index for Segment
	int		offset[16384];				// Segment offset position
    int     PageSize;                   // Page size [bytes]
	struct	SHM_PARAM	*param_ptr;		// Pointer to the Shared Param
	struct	sembuf		sops;			// Semaphore for data access
	unsigned char	*vdifdata_ptr;		// Pointer to shared VDIF data
	float	*xspec_ptr;					// Pointer to 1-sec-integrated Power Spectrum
	FILE	*Pfile_ptr[16];				// Power-Meter File Pointer to write
	FILE	*Afile_ptr[16];				// Autocorr File Pointer to write
	FILE	*Cfile_ptr[16];				// Cross corr File Pointer to write
	char	fname_pre[16];

	//-------- CUDA data
	dim3			Dg, Db(512,1, 1);	// Grid and Block size
	unsigned char	*cuvdifdata_ptr;	// Pointer to VDIF data in GPU
	cufftHandle		cufft_plan;			// 1-D FFT Plan, to be used in cufft
	cufftReal		*cuRealData;		// Time-beased data before FFT, every IF, every segment
	cufftComplex	*cuSpecData;		// FFTed spectrum, every IF, every segment
	float			*cuPowerSpec;		// (autocorrelation) Power Spectrum
	float2			*cuXSpec;           // cross power spectrum
    cudaEvent_t     start, stop;        // Time-mesurement events
	int				modeSW = 0;
    float           elapsed_time_ms;

	//-------- Pointer to functions
 	void	(*segform[4])( unsigned char *, float *, int);
 	segform[0] = segform_1bit;
 	segform[1] = segform_2bit;
 	segform[2] = segform_4bit;
 	segform[3] = segform_8bit;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
//------------------------------------------ Access to the SHARED MEMORY
	shrd_param_id = shmget( SHM_PARAM_KEY, sizeof(struct SHM_PARAM), 0444);
	param_ptr  = (struct SHM_PARAM *)shmat(shrd_param_id, NULL, 0);
	vdifdata_ptr = (unsigned char *)shmat(param_ptr->shrd_vdifdata_id, NULL, SHM_RDONLY);
	xspec_ptr  = (float *)shmat(param_ptr->shrd_xspec_id, NULL, 0);
    PageSize = param_ptr->fsample  / 8 / PAGEPERSEC * param_ptr->qbit;
	switch( param_ptr->qbit ){
 		case  1 :	modeSW = 0; break;
 		case  2 :	modeSW = 1; break;
 		case  4 :	modeSW = 2; break;
 		case  8 :	modeSW = 3; break;
 	}
//------------------------------------------ Prepare for CuFFT
	cudaMalloc( (void **)&cuvdifdata_ptr, PageSize);                                    // for Sampled Data
	cudaMalloc( (void **)&cuRealData, NsegPage* NFFT* sizeof(cufftReal) );              // For FFT segments in a page
	cudaMalloc( (void **)&cuSpecData, NST* NsegPage* NFFTC* sizeof(cufftComplex) );     // For FFTed spectra
	cudaMalloc( (void **)&cuPowerSpec,NST* NFFT2* sizeof(float));                       // For autcorr spectra
	cudaMalloc( (void **)&cuXSpec,    NST* NFFT2* sizeof(float2)/ 2);                   // For cross-corr spectra
	if(cudaGetLastError() != cudaSuccess){
	 	fprintf(stderr, "Cuda Error : Failed to allocate memory.\n"); return(-1); }

 	if(cufftPlan1d(&cufft_plan, NFFT, CUFFT_R2C, NsegPage ) != CUFFT_SUCCESS){
 		fprintf(stderr, "Cuda Error : Failed to create plan.\n"); return(-1); }
//------------------------------------------ Parameters for S-part format
 	segment_offset(param_ptr, offset);
	// for(seg_index=0; seg_index< NsegPage; seg_index++){	printf("Offset[%d] = %d\n", seg_index, offset[seg_index]);}
//------------------------------------------ K5 Header and Data
	cudaMemset( cuPowerSpec, 0, NST* NFFT2* sizeof(float));		// Clear Power Spectrum to accumulate
 	param_ptr->current_rec = -1;
	setvbuf(stdout, (char *)NULL, _IONBF, 0);   // Disable stdout cache
	while(param_ptr->validity & ACTIVE){
		if( param_ptr->validity & (FINISH + ABSFIN) ){  break; }

		//-------- Initial setup for cycles
		cudaMemset( cuPowerSpec, 0, NST* NFFT2* sizeof(float));		// Clear Power Spectrum to accumulate
		cudaMemset( cuXSpec, 0, NST* NFFT2* sizeof(float2)/2);		// Clear Power Spectrum to accumulate

		//-------- Open output files
		if(param_ptr->current_rec == 0){
			sprintf(fname_pre, "%04d%03d%02d%02d%02d", param_ptr->year, param_ptr->doy, param_ptr->hour, param_ptr->min, param_ptr->sec );
			fileRecOpen(param_ptr, fname_pre, Pfile_ptr, Afile_ptr, Cfile_ptr);
		}
		//-------- Wait for S-part memory 
		sops.sem_num = (ushort)SEM_VDIF_PART; sops.sem_op = (short)-1; sops.sem_flg = (short)0;
		semop( param_ptr->sem_data_id, &sops, 1);
		usleep(8);	// Wait 0.01 msec
		// StartTimer();
        cudaEventRecord(start, 0);
        for(threadID=0; threadID < NST; threadID++){
		    //-------- SHM -> GPU memory transfer
		    cudaMemcpy(cuvdifdata_ptr, &vdifdata_ptr[PageSize* (threadID*2 + param_ptr->part_index)], PageSize, cudaMemcpyHostToDevice);
		    //-------- Segment Format
		    Dg.x=NFFT/512; Dg.y=1; Dg.z=1;
		    for(index=0; index < NsegPage; index ++){
			    (*segform[modeSW])<<<Dg, Db>>>( &cuvdifdata_ptr[offset[index]], &cuRealData[index* NFFT], NFFT);
		    }

		    //-------- FFT Real -> Complex spectrum
		    cudaThreadSynchronize();
		    cufftExecR2C(cufft_plan, cuRealData, cuSpecData);		// FFT Time -> Freq
		    cudaThreadSynchronize();

		    //---- Auto Corr
		    Dg.x= NFFT/512; Dg.y=1; Dg.z=1;
		    for(seg_index=0; seg_index<NsegPage; seg_index++){
				accumPowerSpec<<<Dg, Db>>>( &cuSpecData[seg_index* NFFTC], &cuPowerSpec[threadID* NFFT2],  NFFT2);
			}
		}
		//---- Cross Corr
		for(seg_index=0; seg_index<NsegPage; seg_index++){
		    // accumCrossSpec<<<Dg, Db>>>( &cuSpecData[seg_index* NFFTC], &cuSpecData[(seg_index + NsegPage)* NFFTC], cuXSpec,  NFFT2);
		    accumCrossSpec<<<Dg, Db>>>( &cuSpecData[seg_index* NFFTC], &cuSpecData[seg_index* NFFTC], cuXSpec,  NFFT2);
		}
		// printf("%lf [msec]\n", GetTimer());
        cudaEventRecord(stop, 0);
        cudaEventSynchronize(stop);
        cudaEventElapsedTime(&elapsed_time_ms, start, stop);
		printf("%4d %03d %02d:%02d:%02d %8.2f [msec]\n", param_ptr->year, param_ptr->doy, param_ptr->hour, param_ptr->min, param_ptr->sec, elapsed_time_ms);

		//-------- Dump cross spectra to shared memory
		// if( param_ptr->buf_index == PARTNUM - 1){
		cudaMemcpy(xspec_ptr, cuPowerSpec, NST* NFFT2* sizeof(float), cudaMemcpyDeviceToHost);
		sops.sem_num = (ushort)SEM_FX; sops.sem_op = (short)1; sops.sem_flg = (short)0; semop( param_ptr->sem_data_id, &sops, 1);
		for(index=0; index<NST; index++){
		    if(Afile_ptr[index] != NULL){fwrite(&xspec_ptr[index* NFFT2], sizeof(float), NFFT2, Afile_ptr[index]);}   // Save Power Spectra
			if(Pfile_ptr[index] != NULL){fwrite(&(param_ptr->power[index]), sizeof(float), 1, Pfile_ptr[index]);}   // Save Power
		}
		cudaMemcpy(&xspec_ptr[NST* NFFT2], cuXSpec, NFFT2* sizeof(float2), cudaMemcpyDeviceToHost);
		if(Cfile_ptr[0] != NULL){fwrite(&xspec_ptr[NST* NFFT2], sizeof(float2), NFFT2, Cfile_ptr[0]);}   // Save Cross Spectra

	    //-------- Refresh output data file
		if(param_ptr->current_rec == MAX_FILE_REC - 1){
			for(index=0; index<param_ptr->num_st; index++){
				if( Afile_ptr[index] != NULL){   fclose(Afile_ptr[index]);}
				if( Pfile_ptr[index] != NULL){   fclose(Pfile_ptr[index]);}
				if( Cfile_ptr[0] != NULL){   fclose(Cfile_ptr[0]);}
			}
			param_ptr->current_rec = 0;
		} else { param_ptr->current_rec ++;}
		// param_ptr->current_rec ++;
	}	// End of part loop
/*
-------------------------------------------- RELEASE the SHM
*/
	for(index=0; index<param_ptr->num_st; index++){
		if( Afile_ptr[index] != NULL){	fclose(Afile_ptr[index]);}
		if( Pfile_ptr[index] != NULL){	fclose(Pfile_ptr[index]);}
		if( Cfile_ptr[0] != NULL){	fclose(Cfile_ptr[0]);}
	}
	cufftDestroy(cufft_plan);
	cudaFree(cuvdifdata_ptr); cudaFree(cuRealData); cudaFree(cuSpecData); cudaFree(cuPowerSpec); cudaFree(cuXSpec);

    return(0);
}

//-------- Offset to the pointer of  segmant
int	segment_offset(
	struct SHM_PARAM	*param_ptr,	// Pointer to shared parameter
	int					*offset_ptr)
{
	int			seg_index;		// Index for segments
	long long	SegLenByte;		// Length of a segment in Bytes
	SegLenByte = param_ptr->segLen / 8 * param_ptr->qbit;		// Segment Length in Byte
	for(seg_index = 0; seg_index < param_ptr->segPage; seg_index ++){
		offset_ptr[seg_index]= SegLenByte* seg_index;
    }
	return(param_ptr->segPage);
}

//-------- Open Files to Record Data
int	fileRecOpen(
	struct SHM_PARAM	*param_ptr,		// IN: Shared Parameter
	char				*fname_pre,		// IN: File name prefix
	FILE				**Pfile_ptr,	//OUT: file pointer
	FILE				**Afile_ptr,	//OUT: file pointer
	FILE				**Cfile_ptr)	//OUT: file pointer
{
	char				fname[24];
	int					file_index;		// IN: File index number

	for(file_index=0; file_index < param_ptr->num_st; file_index++){
		if( param_ptr->AC_REC & (P00_REC << file_index) ){		// P file
			sprintf(fname, "%s.%c.%02d", fname_pre, 'P', file_index);
			Pfile_ptr[file_index] = fopen(fname, "w");
			fwrite( param_ptr, sizeof(struct SHM_PARAM), 1, Pfile_ptr[file_index]);
		} else { Pfile_ptr[file_index] = NULL;}

		if( param_ptr->AC_REC & (A00_REC << file_index) ){		// A file
			sprintf(fname, "%s.%c.%02d", fname_pre, 'A', file_index);
			Afile_ptr[file_index] = fopen(fname, "w");
			fwrite( param_ptr, sizeof(struct SHM_PARAM), 1, Afile_ptr[file_index]);
		} else { Afile_ptr[file_index] = NULL;}
	}
    if( param_ptr->XC_REC & 0x01){		// C file
        sprintf(fname, "%s.%c.%02d", fname_pre, 'C', 0);
        Cfile_ptr[0] = fopen(fname, "w");
        fwrite( param_ptr, sizeof(struct SHM_PARAM), 1, Cfile_ptr[0]);
    } else { Cfile_ptr[0] = NULL;}
	return(0);
}
