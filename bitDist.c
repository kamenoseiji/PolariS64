//	bitDist.c : bit distribution counter
//
//	Author : Seiji Kameno
//	Created: 2014/08/29
//	Modified: 2016/11/3 for ADS3000+
//
#include "shm_VDIF.inc"
#include <math.h>
#define MAX_LEVEL	4     // Maximum number of digitized levels
#define MAX_LOOP    4      // Maximum number of iterations
#define MAX(a,b)    a>b?a:b // Larger Value

int	bitDist1st2bit(int, unsigned char *, unsigned int *);
int	bitDist2st2bit(int, unsigned char *, unsigned int *);
int	bitDist4st2bit(int, unsigned char *, unsigned int *);
int	bitDist8st2bit(int, unsigned char *, unsigned int *);
int	bitDist16st2bit(int, unsigned char *, unsigned int *);
int VDIFutc();
int gaussBit();

int main(
	int		argc,			// Number of Arguments
	char	**argv )		// Pointer to Arguments
{
	int		shrd_param_id;				// Shared Memory ID
	int		index;						// General Index
	int		cycle_index;				// Index for cycle of buffer (4 cycles per sec)
	int		page_index;					// Index for page in buffer (2 pages per cycle)
	int		seg_index;					// Index for Segment
	int		IF_index;
    int     PageSize;
	struct	SHM_PARAM	*param_ptr;		// Pointer to the Shared Param
	struct	sembuf		sops;			// Semaphore for data access
	unsigned char	*vdifhead_ptr;		// Pointer to the VDIF header
	unsigned char	*vdifdata_ptr;		// Pointer to shared VDIF data
	float	*xspec_ptr;					// Pointer to 1-sec-integrated Power Spectrum
	FILE	*file_ptr[6];				// File Pointer to write
	FILE	*power_ptr[4];				// Power File Pointer to write
	char	fname_pre[16];
	unsigned int		bitStat[64];	// 16 IF x 4 level
	double	param[2], param_err[2];		// Gaussian parameters derived from bit distribution

	int				modeSW = 0;

	//-------- Pointer to functions
 	int	(*bitCount[5])( int, unsigned char *, unsigned int *);
 	bitCount[0] = bitDist1st2bit;
 	bitCount[1] = bitDist2st2bit;
 	bitCount[2] = bitDist4st2bit;
 	bitCount[3] = bitDist8st2bit;
 	bitCount[4] = bitDist16st2bit;

//------------------------------------------ Access to the SHARED MEMORY
	shrd_param_id = shmget( SHM_PARAM_KEY, sizeof(struct SHM_PARAM), 0444);
	param_ptr  = (struct SHM_PARAM *)shmat(shrd_param_id, NULL, 0);
	vdifhead_ptr = (unsigned char *)shmat(param_ptr->shrd_vdifhead_id, NULL, SHM_RDONLY);
	vdifdata_ptr = (unsigned char *)shmat(param_ptr->shrd_vdifdata_id, NULL, SHM_RDONLY);
	switch( param_ptr->num_st ){
 		case  1 :	modeSW = 0; break;
 		case  2 :	modeSW = 1; break;
 		case  4 :	modeSW = 2; break;
 		case  8 :	modeSW = 3; break;
 		case 16 :	modeSW = 4; break;
 	}
//------------------------------------------ VSI Header and Data
    PageSize = param_ptr->fsample  / 8 / 10 * param_ptr->qbit;
 	param_ptr->current_rec = 0;
	setvbuf(stdout, (char *)NULL, _IONBF, 0);   // Disable stdout cache
	while(param_ptr->validity & ACTIVE){
		if( param_ptr->validity & (FINISH + ABSFIN) ){  break; }

		//-------- Loop for half-sec period
		memset(bitStat, 0, sizeof(bitStat));

		//-------- Wait for the first half in the S-part
		sops.sem_num = (ushort)SEM_VDIF_POWER; sops.sem_op = (short)-1; sops.sem_flg = (short)0;
		semop( param_ptr->sem_data_id, &sops, 1);
		usleep(1000);	// Wait 1 msec

		cycle_index = param_ptr->part_index / 2;	// 4 cycles per 1 sec
		page_index  = param_ptr->part_index % 2;	// 2 pages per cycle
		VDIFutc( vdifhead_ptr, param_ptr);
		// printf("Ready to process Part=%d Cycle=%d Page=%d\n", param_ptr->part_index, cycle_index, page_index);

		//-------- BitDist
		// (*bitCount[modeSW])(HALFBUF/4, &vdifdata_ptr[HALFBUF* page_index], bitStat); 
		// printf("Power [dB] = ");
		for(IF_index=0; IF_index<param_ptr->num_st; IF_index ++){
            // bitDist1st2bit(PageSize, &vdifdata_ptr[PageSize* (IF_index + 2* param_ptr->part_index)], &bitStat[4* IF_index]);
            bitDist1st2bit(1048576, &vdifdata_ptr[PageSize* (IF_index + 2* param_ptr->part_index)], &bitStat[4* IF_index]);
		 	gaussBit(4, &bitStat[4* IF_index], param, param_err );
		 	param_ptr->power[IF_index] = 1.0 / (param[0]* param[0]);
		 	// printf("%5.2f ", 10.0* log10(param_ptr->power[IF_index]));
		}
		sops.sem_num = (ushort)SEM_POWER; sops.sem_op = (short)1; sops.sem_flg = (short)0; semop( param_ptr->sem_data_id, &sops, 1);
		// printf("\n");

	}	// End of part loop
/*
-------------------------------------------- RELEASE the SHM
*/
	// for(index=0; index<Nif+2; index++){ if( file_ptr[index] != NULL){	fclose(file_ptr[index]);} }
	// for(index=0; index<Nif; index++){ if( power_ptr[index] != NULL){	fclose(power_ptr[index]);} }

    return(0);
}

//-------- Expected probabilities in quantized level
int probBit(
	int		nlevel,	// IN: Number of quantization levels
	double *param,	// IN: Gaussian mean and sigma
	double *prob)	// OUT:Probabilities in 16 levels
{
	int		index;	// General purpose index
	double	volt[MAX_LEVEL - 1];

	for(index = 0; index < (nlevel - 1); index ++){ volt[index] = param[0]* (double)(index - nlevel/2 + 1);}	// scaled thresh 
	//-------- Calculate probabilities
	prob[0] = 0.5* (erf(M_SQRT1_2*(volt[0] - param[1])) + 1.0);
	for(index = 1; index < (nlevel - 1); index ++){
		prob[index] = 0.5*(erf(M_SQRT1_2*(volt[index] - param[1])) - erf(M_SQRT1_2*(volt[index-1] - param[1])));
	}
	prob[nlevel-1] = 0.5* (1.0 - erf(M_SQRT1_2*(volt[nlevel-2] - param[1])));
	return(0);
}

//-------- Guess initial parameters of Gaussian distribution
int initGaussBit(
	int		nlevel,		// IN: Number of quantization levels
	double	*prob,		// IN: Probabilities in 16 levels
	double	*param)		// OUT:Estimated parameters
{
	double	Vweight;		// Weight for voltage level
	double	Average=0.0;
	double	Variance=0.0;
	int		index;			// General purpose index

	for(index=0; index<nlevel; index++){
		Vweight = (double)(index  - nlevel/2) + 0.5;
		Average += Vweight* prob[index];
		Variance += Vweight* Vweight* prob[index];
	}
	param[0] = 1.0/sqrt(Variance); param[1] = Average* param[0];
	return(0);
}

//-------- Estimate power and bias using bit distribution.
int gaussBit(
	int		nlevel,			// IN : Number of quantization levels
	unsigned int *nsample,	// IN : number of samples in each level
	double	*param,			// OUT: Gaussian parameters 
	double	*param_err)		// OUT: Gaussian parameters 
{
	int		index;					// General index for loops
	int		loop_counter = 0;		// Loop Counter
	unsigned int	total_sample = 0;	// Total number of samples
	double	pwp[2][2];				// Weighted Partial Matrix
	double	prob[MAX_LEVEL];		// Probability in each state
	double	pred[MAX_LEVEL];		// Predicted probability in each state
	double	weight[MAX_LEVEL];		// Weight for Probability 
	double	resid[MAX_LEVEL];		// residual from trial Gaussian
	double	erfDeriv[MAX_LEVEL];	// Vector to produce partial matrix
	double	WerfDeriv[MAX_LEVEL];	// Vector to produce partial matrix
	double	wpr[2];					// WPr vector
	double	solution[2];			// correction vector for parameters
	double	expArg;					// 
	double	det;					// determinant of the partial matrix
	double	norm;					// Norm of the correction vector
	double	epsz;					// criterion for convergence

	//-------- Calculate probability in each state
	for(index=0; index<nlevel; index++){ total_sample += nsample[index]; }	
	for(index=0; index<nlevel; index++){ prob[index] = (double)nsample[index] / (double)total_sample; }	
	for(index=0; index<nlevel; index++){ weight[index] = (double)nsample[index] / ((1.0 - prob[index])* (1.0 - prob[index]))  ; }	
	epsz = MAX(1.0e-6 / (total_sample* total_sample), 1.0e-29);		// Convergence

	initGaussBit(nlevel, prob, param);	// Initial parameter

	while(1){				// Loop for Least-Square Fit
		//-------- Calculate Residual Probability
		probBit(nlevel, param, pred);
		for(index=0; index<nlevel; index++){
			resid[index] = prob[index] - pred[index];
		}

		//-------- Calculate Elements of partial matrix
		erfDeriv[0] = 0.0; WerfDeriv[0] = 0.0;
		for(index=1; index<nlevel; index++){
			expArg = ((double)(index - nlevel/2))* param[0] - param[1];
			erfDeriv[index] = exp( -0.5* expArg* expArg);
			WerfDeriv[index] = ((double)(index - nlevel/2))* erfDeriv[index];
		}
		for(index=0; index<(nlevel-1); index++){
			 erfDeriv[index] = 0.5* M_2_SQRTPI* M_SQRT1_2*( -erfDeriv[index + 1] +  erfDeriv[index]);
			WerfDeriv[index] = 0.5* M_2_SQRTPI* M_SQRT1_2*( WerfDeriv[index + 1] - WerfDeriv[index]);
		}
		erfDeriv[nlevel-1] = 0.5* M_2_SQRTPI* M_SQRT1_2* erfDeriv[nlevel-1];
		WerfDeriv[nlevel-1] = -0.5* M_2_SQRTPI* M_SQRT1_2* WerfDeriv[nlevel-1];

		//-------- Partial Matrix
		memset(pwp, 0, sizeof(pwp)); memset(wpr, 0, sizeof(wpr));
		for(index=0; index<nlevel; index++){
			pwp[0][0] += (WerfDeriv[index]* WerfDeriv[index]* weight[index]);
			pwp[0][1] += (WerfDeriv[index]*  erfDeriv[index]* weight[index]);
			pwp[1][1] += ( erfDeriv[index]*  erfDeriv[index]* weight[index]);
			wpr[0] += (weight[index]* WerfDeriv[index]* resid[index]);
			wpr[1] += (weight[index]*  erfDeriv[index]* resid[index]);
		}
		pwp[1][0] = pwp[0][1];

		//-------- Solutions for correction vectors
		det = pwp[0][0]* pwp[1][1] - pwp[1][0]* pwp[0][1];
		if( fabs(det) < epsz ){	return(-1);	}						// Too small determinant -> Error
		solution[0] = (pwp[1][1]* wpr[0] - pwp[0][1]* wpr[1])/ det;
		solution[1] =(-pwp[1][0]* wpr[0] + pwp[0][0]* wpr[1])/ det;

		//-------- Correction
		param[0] += solution[0];	param[1] += solution[1];	norm = solution[0]*solution[0] + solution[1]*solution[1];

		//-------- Converged?
		loop_counter ++;
		if( norm < epsz ){	break;	}
		if( loop_counter > MAX_LOOP ){	return(-1);	}		// Doesn't converge
	}	// End of iteration loop

	//-------- Standard Error
	param_err[0] = sqrt(pwp[1][1] / det);
	param_err[1] = sqrt(pwp[0][0] / det);
	return(loop_counter);
}

//-------- Convert SoD (Second of Day) into hour, min, and second
int sod2hms(
	int	sod,		// Second of Day
	int	*hour,		// Hour
	int	*min,		// Min
	int	*sec)		// Sec
{
	*hour = sod / 3600;
	*min  = (sod % 3600) / 60;
	*sec  = (sod % 60);
	return(*sec);
}

//-------- UTC in the VDIF header
int	VDIFutc(
	unsigned char		*vdifhead_ptr,	// IN: VDIF header (32 bytes)
	struct SHM_PARAM	*param_ptr)		// OUT: UTC will be set in param_ptr
{
	int	ref_sec = 0;	// Seconds from reference date
	int	ref_epoch = 0;	// Half-year periods from Y2000

	ref_sec    = ((vdifhead_ptr[0] & 0x3f) << 24) + (vdifhead_ptr[1] << 16) + (vdifhead_ptr[2] << 8) + vdifhead_ptr[3];
	ref_epoch  = (vdifhead_ptr[4]      ) & 0x3f;

	param_ptr->year = 2000 + ref_epoch/2;
	param_ptr->doy  =  ref_sec / 86400 + (ref_epoch%2)* 182;
	if(param_ptr->year % 4 == 0){	param_ptr->doy++;}
	sod2hms( ref_sec%86400, &(param_ptr->hour), &(param_ptr->min), &(param_ptr->sec) );

	return(ref_sec);
}
//-------- 2-Bit 1-st Distribution Counter
int bitDist1st2bit(
	int				nbytes,		// Number of bytes to examine
	unsigned char	*data_ptr,	// 2-bit quantized data stream (1 IF)
	unsigned int	*bitStat)	// Bit distribution counter	(1 IF x 4 levels)
{
	int	bitmask = 0x03;			// 2-bit mask
	int	nlevel  = 4;			// Number of levels
	int index;					// Counter
	for(index=0; index<nbytes; index+=4){			// 4 bytes per sample
		bitStat[((data_ptr[index  ]     ) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index  ] >> 2) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index  ] >> 4) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index  ] >> 6) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+1]     ) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+1] >> 2) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+1] >> 4) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+1] >> 6) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+2]     ) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+2] >> 2) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+2] >> 4) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+2] >> 6) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+3]     ) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+3] >> 2) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+3] >> 4) & bitmask)] ++;	// IF-0 bitdist
		bitStat[((data_ptr[index+3] >> 6) & bitmask)] ++;	// IF-0 bitdist
	}
	return(nbytes);
}

//-------- 2-Bit 2-st Distribution Counter
int bitDist2st2bit(
	int				nbytes,		// Number of bytes to examine
	unsigned char	*data_ptr,	// 2-bit quantized data stream (2 IF)
	unsigned int	*bitStat)	// Bit distribution counter	(2 IF x 4 levels)
{
	int	bitmask = 0x03;			// 2-bit mask
	int	nlevel  = 4;			// Number of levels
	int index;					// Counter
	for(index=0; index<nbytes; index+=4){			// 4 bytes per sample
		bitStat[         ((data_ptr[index  ]     ) & bitmask)] ++;	// IF-0 bitdist
		bitStat[         ((data_ptr[index  ] >> 2) & bitmask)] ++;	// IF-0 bitdist
		bitStat[         ((data_ptr[index  ] >> 4) & bitmask)] ++;	// IF-0 bitdist
		bitStat[         ((data_ptr[index  ] >> 6) & bitmask)] ++;	// IF-0 bitdist
		bitStat[         ((data_ptr[index+1]     ) & bitmask)] ++;	// IF-0 bitdist
		bitStat[         ((data_ptr[index+1] >> 2) & bitmask)] ++;	// IF-0 bitdist
		bitStat[         ((data_ptr[index+1] >> 4) & bitmask)] ++;	// IF-0 bitdist
		bitStat[         ((data_ptr[index+1] >> 6) & bitmask)] ++;	// IF-0 bitdist
		bitStat[nlevel + ((data_ptr[index+2]     ) & bitmask)] ++;	// IF-1 bitdist
		bitStat[nlevel + ((data_ptr[index+2] >> 2) & bitmask)] ++;	// IF-1 bitdist
		bitStat[nlevel + ((data_ptr[index+2] >> 4) & bitmask)] ++;	// IF-1 bitdist
		bitStat[nlevel + ((data_ptr[index+2] >> 6) & bitmask)] ++;	// IF-1 bitdist
		bitStat[nlevel + ((data_ptr[index+3]     ) & bitmask)] ++;	// IF-1 bitdist
		bitStat[nlevel + ((data_ptr[index+3] >> 2) & bitmask)] ++;	// IF-1 bitdist
		bitStat[nlevel + ((data_ptr[index+3] >> 4) & bitmask)] ++;	// IF-1 bitdist
		bitStat[nlevel + ((data_ptr[index+3] >> 6) & bitmask)] ++;	// IF-1 bitdist
	}
	return(nbytes);
}

//-------- 2-Bit 4-st Distribution Counter
int bitDist4st2bit(
	int				nbytes,		// Number of bytes to examine
	unsigned char	*data_ptr,	// 2-bit quantized data stream (4 IF)
	unsigned int	*bitStat)	// Bit distribution counter	(4 IF x 4 levels)
{
	int	bitmask = 0x03;			// 2-bit mask
	int	nlevel  = 4;			// Number of levels
	int index;					// Counter
	for(index=0; index<nbytes; index+=4){			// 4 bytes per sample
		bitStat[            ((data_ptr[index  ]     ) & bitmask)] ++;	// IF-0 bitdist
		bitStat[            ((data_ptr[index  ] >> 2) & bitmask)] ++;	// IF-0 bitdist
		bitStat[            ((data_ptr[index  ] >> 4) & bitmask)] ++;	// IF-0 bitdist
		bitStat[            ((data_ptr[index  ] >> 6) & bitmask)] ++;	// IF-0 bitdist
		bitStat[   nlevel + ((data_ptr[index+1]     ) & bitmask)] ++;	// IF-1 bitdist
		bitStat[   nlevel + ((data_ptr[index+1] >> 2) & bitmask)] ++;	// IF-1 bitdist
		bitStat[   nlevel + ((data_ptr[index+1] >> 4) & bitmask)] ++;	// IF-1 bitdist
		bitStat[   nlevel + ((data_ptr[index+1] >> 6) & bitmask)] ++;	// IF-1 bitdist
		bitStat[2* nlevel + ((data_ptr[index+2]     ) & bitmask)] ++;	// IF-2 bitdist
		bitStat[2* nlevel + ((data_ptr[index+2] >> 2) & bitmask)] ++;	// IF-2 bitdist
		bitStat[2* nlevel + ((data_ptr[index+2] >> 4) & bitmask)] ++;	// IF-2 bitdist
		bitStat[2* nlevel + ((data_ptr[index+2] >> 6) & bitmask)] ++;	// IF-2 bitdist
		bitStat[3* nlevel + ((data_ptr[index+3]     ) & bitmask)] ++;	// IF-3 bitdist
		bitStat[3* nlevel + ((data_ptr[index+3] >> 2) & bitmask)] ++;	// IF-3 bitdist
		bitStat[3* nlevel + ((data_ptr[index+3] >> 4) & bitmask)] ++;	// IF-3 bitdist
		bitStat[3* nlevel + ((data_ptr[index+3] >> 6) & bitmask)] ++;	// IF-3 bitdist
	}
	return(nbytes);
}

//-------- 2-Bit 8-st Distribution Counter
int bitDist8st2bit(
	int				nbytes,		// Number of bytes to examine
	unsigned char	*data_ptr,	// 2-bit quantized data stream (8 IF)
	unsigned int	*bitStat)	// Bit distribution counter	(8 IF x 4 levels)
{
	int	bitmask = 0x03;			// 2-bit mask
	int	nlevel  = 4;			// Number of levels
	int index;					// Counter
	for(index=0; index<nbytes; index+=4){			// 4 bytes per sample
		bitStat[            ((data_ptr[index  ] >> 6) & bitmask)] ++;	// IF-0 bitdist
		bitStat[            ((data_ptr[index  ] >> 4) & bitmask)] ++;	// IF-0 bitdist
		bitStat[   nlevel + ((data_ptr[index  ] >> 2) & bitmask)] ++;	// IF-1 bitdist
		bitStat[   nlevel + ((data_ptr[index  ]     ) & bitmask)] ++;	// IF-1 bitdist
		bitStat[2* nlevel + ((data_ptr[index+1] >> 6) & bitmask)] ++;	// IF-2 bitdist
		bitStat[2* nlevel + ((data_ptr[index+1] >> 4) & bitmask)] ++;	// IF-2 bitdist
		bitStat[3* nlevel + ((data_ptr[index+1] >> 2) & bitmask)] ++;	// IF-3 bitdist
		bitStat[3* nlevel + ((data_ptr[index+1]     ) & bitmask)] ++;	// IF-3 bitdist
		bitStat[4* nlevel + ((data_ptr[index+2] >> 6) & bitmask)] ++;	// IF-4 bitdist
		bitStat[4* nlevel + ((data_ptr[index+2] >> 4) & bitmask)] ++;	// IF-4 bitdist
		bitStat[5* nlevel + ((data_ptr[index+2] >> 2) & bitmask)] ++;	// IF-5 bitdist
		bitStat[5* nlevel + ((data_ptr[index+2]     ) & bitmask)] ++;	// IF-5 bitdist
		bitStat[6* nlevel + ((data_ptr[index+3] >> 6) & bitmask)] ++;	// IF-6 bitdist
		bitStat[6* nlevel + ((data_ptr[index+3] >> 4) & bitmask)] ++;	// IF-6 bitdist
		bitStat[7* nlevel + ((data_ptr[index+3] >> 2) & bitmask)] ++;	// IF-7 bitdist
		bitStat[7* nlevel + ((data_ptr[index+3]     ) & bitmask)] ++;	// IF-7 bitdist
	}
	return(nbytes);
}

//-------- 2-Bit 16-st Distribution Counter
int bitDist16st2bit(
	int				nbytes,		// Number of bytes to examine
	unsigned char	*data_ptr,	// 2-bit quantized data stream (16 IF)
	unsigned int	*bitStat)	// Bit distribution counter	(16 IF x 4 levels)
{
	int	bitmask = 0x03;			// 2-bit mask
	int	nlevel  = 4;			// Number of levels
	int index;					// Counter

	for(index=0; index<nbytes; index+=4){			// 4 bytes per sample
		bitStat[             ((data_ptr[index  ] >> 6) & bitmask)] ++;	// IF-0 bitdist
		bitStat[    nlevel + ((data_ptr[index  ] >> 4) & bitmask)] ++;	// IF-1 bitdist
		bitStat[ 2* nlevel + ((data_ptr[index  ] >> 2) & bitmask)] ++;	// IF-2 bitdist
		bitStat[ 3* nlevel + ((data_ptr[index  ]     ) & bitmask)] ++;	// IF-3 bitdist
		bitStat[ 4* nlevel + ((data_ptr[index+1] >> 6) & bitmask)] ++;	// IF-4 bitdist
		bitStat[ 5* nlevel + ((data_ptr[index+1] >> 4) & bitmask)] ++;	// IF-5 bitdist
		bitStat[ 6* nlevel + ((data_ptr[index+1] >> 2) & bitmask)] ++;	// IF-6 bitdist
		bitStat[ 7* nlevel + ((data_ptr[index+1]     ) & bitmask)] ++;	// IF-7 bitdist
		bitStat[ 8* nlevel + ((data_ptr[index+2] >> 6) & bitmask)] ++;	// IF-8 bitdist
		bitStat[ 9* nlevel + ((data_ptr[index+2] >> 4) & bitmask)] ++;	// IF-9 bitdist
		bitStat[10* nlevel + ((data_ptr[index+2] >> 2) & bitmask)] ++;	// IF-10 bitdist
		bitStat[11* nlevel + ((data_ptr[index+2]     ) & bitmask)] ++;	// IF-11 bitdist
		bitStat[12* nlevel + ((data_ptr[index+3] >> 6) & bitmask)] ++;	// IF-12 bitdist
		bitStat[13* nlevel + ((data_ptr[index+3] >> 4) & bitmask)] ++;	// IF-13 bitdist
		bitStat[14* nlevel + ((data_ptr[index+3] >> 2) & bitmask)] ++;	// IF-14 bitdist
		bitStat[15* nlevel + ((data_ptr[index+3]     ) & bitmask)] ++;	// IF-15 bitdist
	}
	return(nbytes);
}

