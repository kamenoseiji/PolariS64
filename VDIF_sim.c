//	VDIF_sim.c : Store VDIF data from Octavia and Store to Shared Memory
//
//	Author : Seiji Kameno
//	Created: 2014/09/16
//
#include "shm_VDIF.inc"
#include <errno.h>
int main(
	int		argc,			// Number of Arguments
	char	**argv )		// Pointer to Arguments
{
	int		shrd_param_id;				// Shared Memory ID
	struct	SHM_PARAM	*param_ptr;		// Pointer to the Shared Param
	struct	sembuf		sops;			// Semaphore
	int		rv;							// Return Value from K5/VSSP32
	unsigned char	*vdifhead_ptr;		// Pointer to the shared K5 header
	unsigned char	*vdifdata_ptr;		// Pointer to the shared K5 data
	unsigned char	*shm_write_ptr;		// Writing Pointer
	int		frameID;					// Frame ID
	int		frame_addr;					// Write address in page
	int		prev_part  = -1;			// Part Index
	FILE	*dumpfile_ptr;				// Dump File

	unsigned char	buf[VDIF_SIZE];		// 1312 bytes
//------------------------------------------ Open Socket to OCTAVIA
	memset(buf, 0, sizeof(buf));
//------------------------------------------ Access to the SHARED MEMORY
    //-------- SHARED PARAMETERS --------
    if(shm_access(
        SHM_PARAM_KEY,					// ACCESS KEY
        sizeof(struct SHM_PARAM),		// SIZE OF SHM
        &shrd_param_id,					// SHM ID
        &param_ptr) == -1){				// Pointer to the SHM
		perror("  Error : Can't access to the shared memory!!");
		return(-1);
	}
	printf("Succeeded to access the shared parameter [%d]!\n",  param_ptr->shrd_param_id);
//------------------------------------------ Access the Dump File
	if( (dumpfile_ptr = fopen(argv[1], "r")) == NULL){
		perror(" Can't open dump file!!");	return(-1);}

    //-------- SHARED VDIF Header and Data to Store --------
	vdifhead_ptr = shmat( param_ptr->shrd_vdifhead_id, NULL, 0 );
	vdifdata_ptr = shmat( param_ptr->shrd_vdifdata_id, NULL, 0 );
	param_ptr->validity |= ENABLE;		// Set Shared memory readiness bit to 1
//------------------------------------------ Open Socket to OCTAVIA
	setvbuf(stdout, (char *)NULL, _IONBF, 0); 	// Disable stdout cache
	param_ptr->validity |= ACTIVE;		// Set Sampling Activity Bit to 1

 	while( param_ptr->validity & ACTIVE ){
		if( param_ptr->validity & (FINISH + ABSFIN) ){	break; }

		//-------- Read VDIF from File
		rv = fread( buf, VDIF_SIZE, 1, dumpfile_ptr );
		if(rv == 0){
			printf("Rewind to file head\n");
			rewind(dumpfile_ptr); continue;
		}
		frameID    = (buf[5] << 16) + (buf[6] << 8) + buf[7];
		param_ptr->part_index = frameID / FramePerPart;				// Part Number (0 - 7)
		frame_addr = VDIFDATA_SIZE* (frameID % FramePerBUF);		// Write Address in Page
		memcpy( vdifhead_ptr, buf, VDIFHEAD_SIZE);
		memcpy( &vdifdata_ptr[frame_addr], &buf[VDIFHEAD_SIZE], VDIFDATA_SIZE);

		usleep(1);
		if( param_ptr->part_index != prev_part){
			sops.sem_num = (ushort)SEM_VDIF_PART; sops.sem_op = (short)1; sops.sem_flg = (short)0;
			semop(param_ptr->sem_data_id, &sops, 1);
			sops.sem_num = (ushort)SEM_VDIF_POWER; sops.sem_op = (short)1; sops.sem_flg = (short)0;
			semop(param_ptr->sem_data_id, &sops, 1);
			prev_part = param_ptr->part_index;
		}
	}
//------------------------------------------ Stop Sampling
	fclose(dumpfile_ptr);
	param_ptr->validity &= (~ACTIVE);		// Set Sampling Activity Bit to 0

    return(0);
}
