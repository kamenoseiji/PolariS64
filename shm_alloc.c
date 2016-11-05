//	shm_alloc.c : Open Shared Memory Area
//
//	Author : Seiji Kameno
//	Created: 2012/10/18
//
#include "shm_VDIF.inc"

int main(
	int		argc,			// Number of Arguments
	char	**argv )		// Pointer to Arguments
{
	int		shrd_param_id;				// Shared Memory ID
    int     TotalPageSize;              // Size of shared VDIF Data
    int     TotalSpecSize;              // Size of shared Xspec Data
	struct	SHM_PARAM	*param_ptr;		// Pointer to the Shared Param
	struct	sembuf	sops;				// Semaphore for data area
	unsigned char	*vdifhead_ptr;		// Pointer to the Shared VDIF Data 
	unsigned char	*vdifdata_ptr;		// Pointer to the Shared VDIF Data 
	float	*segdata_ptr;				// Pointer to the shared segment data
	float	*xspec_ptr;					// Pointer to the cross-power spectra
	int		index;
	FILE	*shm_param_fptr;			// File to record shared memory information
	FILE	*shm_vdifh_fptr;			// File to record shared memory information
	FILE	*shm_vdifd_fptr;			// File to record shared memory information
	FILE	*shm_xspec_fptr;			// File to record shared memory information
//------------------------------------------ Access to the Shared Param
	if(shm_access(SHM_PARAM_KEY, sizeof(struct SHM_PARAM), &shrd_param_id, &param_ptr) == -1){
		perror("  Error : shm_alloc() can't access to the shared memory!!");   return(-1);
	};
	shm_param_fptr = fopen(SHM_PARAM_FILE, "w");
	fprintf(shm_param_fptr, SHM_FMT, SHM_PARAM_KEY, shrd_param_id, sizeof(struct SHM_PARAM) );
	fclose(shm_param_fptr);
//------------------------------------------ ALLOC SHARED MEMORY
	//-------- Semaphore for data area
	param_ptr->sem_data_id = semget(SEM_DATA_KEY, SEM_NUM, IPC_CREAT | 0666);
	for(index=0; index<SEM_NUM; index++){
		sops.sem_num = (ushort)index;
		sops.sem_op = (short)0;
		sops.sem_flg = IPC_NOWAIT;
		semop( param_ptr->sem_data_id, &sops, 1);
	}

    //-------- SHARED VDIF HEADER AREA --------
	if(shm_init_create(
		VDIFHEAD_KEY,					// ACCESS KEY
		NST* VDIFHEAD_SIZE,			    // Data Area Size
		&(param_ptr->shrd_vdifhead_id),	// SHM ID
		&vdifhead_ptr) == -1){			// Pointer to the shared data
		perror("Can't Create Shared VDIF header!!\n"); return(-1);
	}
	memset(vdifhead_ptr, 0x00, VDIFHEAD_SIZE);
	printf("Allocated %d bytes for Shared VDIF header [%d]!\n", NST* VDIFHEAD_SIZE, param_ptr->shrd_vdifhead_id);
	shm_vdifh_fptr = fopen(SHM_VDIFH_FILE, "w");
	fprintf(shm_vdifh_fptr, SHM_FMT, VDIFHEAD_KEY, param_ptr->shrd_vdifhead_id, NST* VDIFHEAD_SIZE);
	fclose(shm_vdifh_fptr);

    //-------- SHARED VDIF DATA AREA --------
    TotalPageSize = (param_ptr->fsample / PAGENUM) / 8 * param_ptr->qbit* NST* PAGENUM;
	if(shm_init_create(
		VDIFDATA_KEY,					// ACCESS KEY
		TotalPageSize,	                // Data Area Size
		&(param_ptr->shrd_vdifdata_id),	// SHM ID
		&vdifdata_ptr) == -1){			// Pointer to the shared data
		perror("Can't Create Shared VDIF data area!!\n"); return(-1);
	}
	memset(vdifdata_ptr, 0x00, TotalPageSize);
	printf("Allocated %d bytes for Shared VDIF data [%d]!\n", TotalPageSize, param_ptr->shrd_vdifdata_id);
	shm_vdifd_fptr = fopen(SHM_VDIFD_FILE, "w");
	fprintf(shm_vdifd_fptr, SHM_FMT, VDIFDATA_KEY, param_ptr->shrd_vdifdata_id, TotalPageSize);
	fclose(shm_vdifd_fptr);

    //-------- SHARED cross-power-spectra data area --------
    TotalSpecSize = 2* NST* param_ptr->num_ch* sizeof(float);
	// printf("Trying to allocate %lu bytes for Shared Xspec data [KEY = %d]!\n", TotalSpecSize, XSPEC_KEY);
	if(shm_init_create(
		XSPEC_KEY,						// ACCESS KEY
		TotalSpecSize,					// Data Area Size
		&(param_ptr->shrd_xspec_id),	// SHM ID
		&xspec_ptr) == -1){				// Pointer to the shared segment data
		perror("Can't Create Shared XSPEC data!!\n"); return(-1);
	}
	printf("Allocated %d bytes for Shared Xspec data [%d]!\n", TotalSpecSize, param_ptr->shrd_xspec_id);
	shm_xspec_fptr = fopen(SHM_XSPEC_FILE, "w");
	fprintf(shm_xspec_fptr, SHM_FMT, XSPEC_KEY, param_ptr->shrd_xspec_id, TotalSpecSize);
	fclose(shm_xspec_fptr);
//------------------------------------------ End of Process
    return(0);
}
