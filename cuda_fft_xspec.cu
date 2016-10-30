//	cuda_fft_xspec.c : FFT using CuFFT
//
//	Author : Seiji Kameno
//	Created: 2012/12/6
//
#include <cuda.h>
#include <cufft.h>
#include <string.h>
#include <math.h>
#include </usr/local/cuda/samples/common/inc/timer.h>
#include "cuda_polaris.inc"
#define SCALEFACT 1.0/(NFFT* NsegSec)

int	segment_offset(struct SHM_PARAM	*, int *);
int	fileRecOpen(struct SHM_PARAM	*, char *, FILE **, FILE **);

main(
	int		argc,			// Number of Arguments
	char	**argv )		// Pointer to Arguments
{
	int		shrd_param_id;				// Shared Memory ID
	int		index;						// General Index
	int		page_index;					// Index for page in buffer (2 pages per cycle)
	int		seg_index;					// Index for Segment
	int		offset[16384];				// Segment offset position
	struct	SHM_PARAM	*param_ptr;		// Pointer to the Shared Param
	struct	sembuf		sops;			// Semaphore for data access
	unsigned char	*vdifdata_ptr;		// Pointer to shared VDIF data
	float	*xspec_ptr;					// Pointer to 1-sec-integrated Power Spectrum
	FILE	*Pfile_ptr[16];				// File Pointer to write
	FILE	*Afile_ptr[16];				// File Pointer to write
	char	fname_pre[16];

	dim3			Dg, Db(512,1, 1);	// Grid and Block size
	unsigned char	*cuvdifdata_ptr;	// Pointer to VDIF data in GPU
	cufftHandle		cufft_plan;			// 1-D FFT Plan, to be used in cufft
	cufftReal		*cuRealData;		// Time-beased data before FFT, every IF, every segment
	cufftComplex	*cuSpecData;		// FFTed spectrum, every IF, every segment
	float			*cuPowerSpec;		// (autocorrelation) Power Spectrum
	// float2			*cuXSpec;
	int				modeSW = 0;

	//-------- Pointer to functions
 	void	(*segform[5])( unsigned char *, float *, int);
 	segform[0] = segform_1st_2bit;
 	segform[1] = segform_2st_2bit;
 	segform[2] = segform_4st_2bit;
 	segform[3] = segform_8st_2bit;
 	segform[4] = segform_16st_2bit;
//------------------------------------------ Access to the SHARED MEMORY
	shrd_param_id = shmget( SHM_PARAM_KEY, sizeof(struct SHM_PARAM), 0444);
	param_ptr  = (struct SHM_PARAM *)shmat(shrd_param_id, NULL, 0);
	vdifdata_ptr = (unsigned char *)shmat(param_ptr->shrd_vdifdata_id, NULL, SHM_RDONLY);
	xspec_ptr  = (float *)shmat(param_ptr->shrd_xspec_id, NULL, 0);
	switch( param_ptr->num_st ){
 		case  1 :	modeSW = 0; break;
 		case  2 :	modeSW = 1; break;
 		case  4 :	modeSW = 2; break;
 		case  8 :	modeSW = 3; break;
 		case 16 :	modeSW = 4; break;
 	}
//------------------------------------------ Prepare for CuFFT
	cudaMalloc( (void **)&cuvdifdata_ptr, MAX_SAMPLE_BUF);
	cudaMalloc( (void **)&cuRealData, NST* NsegPart* NFFT * sizeof(cufftReal) );
	cudaMalloc( (void **)&cuSpecData, NST* NsegPart* NFFTC* sizeof(cufftComplex) );
	cudaMalloc( (void **)&cuPowerSpec, NST* NFFT2* sizeof(float));
	// cudaMalloc( (void **)&cuXSpec, 2* NFFT2* sizeof(float2));

	if(cudaGetLastError() != cudaSuccess){
	 	fprintf(stderr, "Cuda Error : Failed to allocate memory.\n"); return(-1); }

 	if(cufftPlan1d(&cufft_plan, NFFT, CUFFT_R2C, NST* NsegPart ) != CUFFT_SUCCESS){
 		fprintf(stderr, "Cuda Error : Failed to create plan.\n"); return(-1); }
//------------------------------------------ Parameters for S-part format
 	segment_offset(param_ptr, offset);
	// for(index=0; index< 2*NsegPart; index++){	printf("Offset[%d] = %d\n", index, offset[index]);}
//------------------------------------------ K5 Header and Data
	cudaMemset( cuPowerSpec, 0, NST* NFFT2* sizeof(float));		// Clear Power Spectrum to accumulate
 	param_ptr->current_rec = 0;
	setvbuf(stdout, (char *)NULL, _IONBF, 0);   // Disable stdout cache
	while(param_ptr->validity & ACTIVE){
		if( param_ptr->validity & (FINISH + ABSFIN) ){  break; }

		//-------- Initial setup for cycles
		if( param_ptr->buf_index == 0){
			cudaMemset( cuPowerSpec, 0, NST* NFFT2* sizeof(float));		// Clear Power Spectrum to accumulate
		}

		//-------- Open output files
		if(param_ptr->current_rec == 0){
			sprintf(fname_pre, "%04d%03d%02d%02d%02d", param_ptr->year, param_ptr->doy, param_ptr->hour, param_ptr->min, param_ptr->sec );
			fileRecOpen(param_ptr, fname_pre, Pfile_ptr, Afile_ptr);
		}

		//-------- Wait for the first half in the S-part
		sops.sem_num = (ushort)SEM_VDIF_PART; sops.sem_op = (short)-1; sops.sem_flg = (short)0;
		semop( param_ptr->sem_data_id, &sops, 1);
		usleep(8);	// Wait 0.01 msec
		// StartTimer();
		page_index = param_ptr->buf_index % 2;			// 2 pages per cycle
		printf("... Ready to process Part=%d Page=%d\n", param_ptr->buf_index, page_index);

		//-------- SHM -> GPU memory transfer
		cudaMemcpy( &cuvdifdata_ptr[HALFBUF* page_index], &vdifdata_ptr[HALFBUF* page_index], HALFBUF, cudaMemcpyHostToDevice);
		//-------- Segment Format
		Dg.x=NFFT/512; Dg.y=1; Dg.z=1;
		for(index=0; index < NsegPart; index ++){
			seg_index = page_index* NsegPart + index;
			(*segform[modeSW])<<<Dg, Db>>>( &cuvdifdata_ptr[offset[seg_index]], &cuRealData[index* NST* NFFT], NFFT);
		}

		//-------- FFT Real -> Complex spectrum
		cudaThreadSynchronize();
		cufftExecR2C(cufft_plan, cuRealData, cuSpecData);		// FFT Time -> Freq
		cudaThreadSynchronize();

		//---- Auto Corr
		Dg.x= NFFTC/512; Dg.y=1; Dg.z=1;
		for(seg_index=0; seg_index<NsegPart; seg_index++){
			 for(index=0; index<NST; index++){
				accumPowerSpec<<<Dg, Db>>>( &cuSpecData[(seg_index* NST + index)* NFFTC], &cuPowerSpec[index* NFFT2],  NFFT2);
			}
		}

		//-------- Dump cross spectra to shared memory
		if( param_ptr->buf_index == PARTNUM - 1){
			cudaMemcpy(xspec_ptr, cuPowerSpec, NST* NFFT2* sizeof(float), cudaMemcpyDeviceToHost);
			sops.sem_num = (ushort)SEM_FX; sops.sem_op = (short)1; sops.sem_flg = (short)0; semop( param_ptr->sem_data_id, &sops, 1);
			for(index=0; index<param_ptr->num_st; index++){
				if(Afile_ptr[index] != NULL){fwrite(&xspec_ptr[index* NFFT2], sizeof(float), NFFT2, Afile_ptr[index]);}   // Save Power Spectra
				if(Pfile_ptr[index] != NULL){fwrite(&(param_ptr->power[index]), sizeof(float), 1, Pfile_ptr[index]);}   // Save Power
			}

			//-------- Refresh output data file
			if(param_ptr->current_rec == MAX_FILE_REC - 1){
				for(index=0; index<param_ptr->num_st; index++){
					if( Afile_ptr[index] != NULL){   fclose(Afile_ptr[index]);}
					if( Pfile_ptr[index] != NULL){   fclose(Pfile_ptr[index]);}
				}
				param_ptr->current_rec = 0;
			} else { param_ptr->current_rec ++;}
		}
		param_ptr->current_rec ++;
		// printf("%lf [msec]\n", GetTimer());
	}	// End of part loop
/*
-------------------------------------------- RELEASE the SHM
*/
	for(index=0; index<param_ptr->num_st; index++){
		if( Afile_ptr[index] != NULL){	fclose(Afile_ptr[index]);}
		if( Pfile_ptr[index] != NULL){	fclose(Pfile_ptr[index]);}
	}
	cufftDestroy(cufft_plan);
	cudaFree(cuvdifdata_ptr); cudaFree(cuRealData); cudaFree(cuSpecData); cudaFree(cuPowerSpec); // cudaFree(cuXSpec);

    return(0);
}

//-------- Offset to the pointer of  segmant
int	segment_offset(
	struct SHM_PARAM	*param_ptr,	// Pointer to shared parameter
	int					*offset_ptr)
{
	int			seg_index;		// Index for segments
	long long	SegLenByte;		// Length of a segment in Bytes

	//-------- First Half
	SegLenByte = param_ptr->segLen* param_ptr->qbit* param_ptr->num_st / 8;		// Segment Length in Byte
	for(seg_index = 0; seg_index < NsegPart; seg_index ++){

		offset_ptr[seg_index]  = (int)(((long long)seg_index* (HALFBUF - SegLenByte))/ ((long long)NsegPart - 1));
		offset_ptr[seg_index]  -= (offset_ptr[seg_index] % 4);		// 4-byte alignment

		offset_ptr[seg_index + NsegPart]= offset_ptr[seg_index] + HALFBUF;
	}
	return(NsegSec);
}

//-------- Open Files to Record Data
int	fileRecOpen(
	struct SHM_PARAM	*param_ptr,		// IN: Shared Parameter
	char				*fname_pre,		// IN: File name prefix
	FILE				**Pfile_ptr,		//OUT: file pointer
	FILE				**Afile_ptr)		//OUT: file pointer
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
	return(0);
}
