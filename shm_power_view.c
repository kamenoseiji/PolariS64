//	shm_power_view.c : Total Power Monitor
//
//	Author : Seiji Kameno
//	Created: 2013/12/23
//
#include "shm_VDIF.inc"
#include <stdlib.h>
#include <cpgplot.h>
#include <math.h>

int cpg_setup();
int cpg_power();

int main(
	int		argc,			// Number of Arguments
	char	**argv )		// Pointer to Arguments
{
	int		shrd_param_id;				// Shared Memory ID
	struct	SHM_PARAM	*param_ptr;		// Pointer to the Shared Param
	struct	sembuf		sops;			// Semaphore for data area
	int		IFindex;
	int		index;
	float	bitPower[MAX_NIF][POWER_TIME_NUM];	// Power Monitor Data
	float	tempPower[POWER_TIME_NUM];			// Buffer
	char	pg_text[256];				// Text to plot
	char	xlabel[64];					// X-axis label
//------------------------------------------ Access to the SHARED MEMORY
    //------- SHARED PARAMETERS --------
    if(shm_access(
        SHM_PARAM_KEY,					// ACCESS KEY
        sizeof(struct SHM_PARAM),		// SIZE OF SHM
        &shrd_param_id,					// SHM ID
        &param_ptr) != -1){				// Pointer to the SHM
		printf("PowerView: Succeeded to access the shared parameter [%d]!\n",  param_ptr->shrd_param_id);
	}
	memset(bitPower, 0, param_ptr->num_st* POWER_TIME_NUM* sizeof(float));
//------------------------------------------ K5 Header and Data
	setvbuf(stdout, (char *)NULL, _IONBF, 0);	// Disable stdout cache
	cpgbeg(1, argv[1], 1, 1);

	while(param_ptr->validity & ACTIVE){
		cpgbbuf();
		sprintf(xlabel, "Elapsed Time [sec]"); cpg_setup(xlabel);
		if( param_ptr->validity & (FINISH + ABSFIN) ){  break; }

		//-------- Wait for Semaphore
		sops.sem_num = (ushort)SEM_POWER;	sops.sem_op = (short)-1;	sops.sem_flg = (short)0;
		semop( param_ptr->sem_data_id, &sops, 1);

		//-------- Plot Power Monitor
		for(IFindex=0; IFindex<NST; IFindex++){
			memcpy(tempPower, bitPower[IFindex], POWER_TIME_NUM*sizeof(float));
			memcpy(&bitPower[IFindex][1], tempPower, (POWER_TIME_NUM-1)*sizeof(float));
			bitPower[IFindex][0] = 10.0* log10(param_ptr->power[IFindex]);
		}
		cpg_power(param_ptr, bitPower);
	}
	cpgend();
//------------------------------------------ RELEASE the SHM
    return(0);
}
