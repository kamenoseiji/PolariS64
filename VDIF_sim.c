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
	int		frameID, threadID;			// Frame ID and tread (=stream) ID
	int		addr_offset;				// Address from the head of the page
    int     PageSize, page_index;       // Bytes per page and index of page
    int     FramePerPage;               // Number of frames (1280 bytes) per page
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
    PageSize = param_ptr->fsample  / 8 / 10 * param_ptr->qbit;    // Page size [bytes]
    FramePerPage = PageSize / VDIFDATA_SIZE;
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
        threadID   = ((buf[12] & 0x03) << 8 ) + buf[13] - 1;
        page_index =  (frameID / FramePerPage) % 10;
        addr_offset = PageSize* (threadID + 2* (page_index & 0x01)) +  (frameID % FramePerPage)* VDIFDATA_SIZE;
        memcpy( &vdifdata_ptr[addr_offset], &buf[VDIFHEAD_SIZE], VDIFDATA_SIZE);
		usleep(1);
        if(frameID % FramePerPage == FramePerPage - 1){
            param_ptr->part_index = page_index & 0x01;
            memcpy( vdifhead_ptr, buf, VDIFHEAD_SIZE);
            param_ptr->validity |= ENABLE;
			sops.sem_num = (ushort)SEM_VDIF_PART; sops.sem_op = (short)1; sops.sem_flg = (short)0;
			semop(param_ptr->sem_data_id, &sops, 1);
			sops.sem_num = (ushort)SEM_VDIF_POWER; sops.sem_op = (short)1; sops.sem_flg = (short)0;
			semop(param_ptr->sem_data_id, &sops, 1);
		}
	}
//------------------------------------------ Stop Sampling
	fclose(dumpfile_ptr);
	param_ptr->validity &= (~ACTIVE);		// Set Sampling Activity Bit to 0

    return(0);
}
