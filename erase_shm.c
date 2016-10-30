//	erase_shm.c : Erase Shared Memory Area
//
//	Author : Seiji Kameno
//	Created: 2012/10/18
//
#include "shm_VDIF.inc"

int erase_shm(
	struct SHM_PARAM	*param_ptr )	// Pointer to the Shared Param
{
//-------- RELEASE SHARED MEMORY
	int		index;						// General counter

	shmctl(param_ptr->shrd_xspec_id, IPC_RMID, 0);		// Release xspec data
	shmctl(param_ptr->shrd_vdifdata_id, IPC_RMID, 0);	// Release VDIF data
	shmctl(param_ptr->shrd_vdifhead_id, IPC_RMID, 0);	// Release VDIF header

	sleep(1);
	semctl(param_ptr->sem_data_id, 0, IPC_RMID, 0);		// Release Semaphore
	sleep(1);
	shmctl(param_ptr->shrd_param_id, IPC_RMID, 0);		// Release shared param 

    return(0);
}
