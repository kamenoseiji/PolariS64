//	VDIF_store.c : Store VDIF data from Octavia and Store to Shared Memory
//
//	Author : Seiji Kameno
//	Created: 2014/08/26
//
#include "shm_VDIF.inc"
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
// #include <stdint.h>
// #include <sys/ioctl.h>


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
	int		sock;						// Socket ID descriptor
	int		frameID, prevFrameID;		// Frame ID
	int		frame_addr;					// Write address in page
	int		accum_index  = 0;			// Accumulated part Index
	struct sockaddr_in	addr;			//  Socket Address
	struct ip_mreq		mreq;			// Multicast Request
	FILE	*dumpfile_ptr;				// Dump File
    int     dump_counter = 16;

	unsigned char	buf[VDIF_SIZE];		// 1312 bytes
//------------------------------------------ Open Socket to OCTAVIA
	// sock = socket(PF_INET, SOCK_DGRAM, 0);
	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if(sock < 0){
		perror("Socket Failed\n"); printf("%d\n", errno);
		return(-1);
	}
	// addr.sin_family = PF_INET;
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(60000);
	// addr.sin_addr.s_addr    = inet_addr("192.168.10.163");
	addr.sin_addr.s_addr    = INADDR_ANY;
	if( bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0){
        perror("Bind Failed\n"); printf("%d\n", errno);
    }
	memset(buf, 0, sizeof(buf));
    //-------- Multicast
	// memset(&mreq, 0, sizeof(mreq));
	// mreq.imr_interface.s_addr = INADDR_ANY;
	// mreq.imr_multiaddr.s_addr = inet_addr("239.192.1.2");
	// if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char *)&mreq, sizeof(mreq)) != 0){
	// 	perror(" Error: setsockopt");
	// 	return(-1);
	// }
//------------------------------------------ Access to the SHARED MEMORY
    //-------- SHARED PARAMETERS --------
    if(shm_access(
        SHM_PARAM_KEY,					// ACCESS KEY
        sizeof(struct SHM_PARAM),		// SIZE OF SHM
        &shrd_param_id,					// SHM ID
        &param_ptr) == -1){				// Pointer to the SHM
		perror("  Error : Can't access to the shared memory!!");
		close(sock); return(-1);
	}
	printf("VDIF_store: Succeeded to access the shared parameter [%d]!\n",  param_ptr->shrd_param_id);

    //-------- SHARED VDIF Header and Data to Store --------
	vdifhead_ptr = shmat( param_ptr->shrd_vdifhead_id, NULL, 0 );
	vdifdata_ptr = shmat( param_ptr->shrd_vdifdata_id, NULL, 0 );
	param_ptr->validity |= ENABLE;		// Set Shared memory readiness bit to 1
	if(argc > 1){
		dumpfile_ptr = fopen(argv[1], "w");
	}
//------------------------------------------ Open Socket to OCTAVIA
	setvbuf(stdout, (char *)NULL, _IONBF, 0); 	// Disable stdout cache
	param_ptr->validity |= ACTIVE;		// Set Sampling Activity Bit to 1
	param_ptr->validity &= (~ENABLE);	// Wait until first second 

 	while( param_ptr->validity & ACTIVE ){
		if( param_ptr->validity & (FINISH + ABSFIN) ){	break; }

		//-------- Read VDIF packet
		rv = recv(sock, buf, sizeof(buf), 0);
		memcpy( vdifhead_ptr, buf, VDIFHEAD_SIZE);
		if(argc > 1){
		  	fwrite(buf, VDIF_SIZE, 1, dumpfile_ptr);
            dump_counter --;
            argc = dump_counter;
		}
		frameID    = (buf[5] << 16) + (buf[6] << 8) + buf[7];
		param_ptr->part_index = frameID / FramePerPart;				// Part Number (0 - 7)
		frame_addr = VDIFDATA_SIZE* (frameID % FramePerBUF);		// Write Address in BUF
		memcpy( &vdifdata_ptr[frame_addr], &buf[VDIFHEAD_SIZE], VDIFDATA_SIZE);

		if( frameID % FramePerPart == FramePerPart - 1){ 
			accum_index ++;
			if(accum_index > 8){
				param_ptr->buf_index = param_ptr->part_index;			// Buffer Number (0 - 7)
				printf("Part%d : frame ID = %06d : frameAddr = %d \n", param_ptr->part_index, frameID, frame_addr);
				param_ptr->validity |= ENABLE;
				sops.sem_num = (ushort)SEM_VDIF_PART; sops.sem_op = (short)1; sops.sem_flg = (short)0;
				semop(param_ptr->sem_data_id, &sops, 1);
				sops.sem_num = (ushort)SEM_VDIF_POWER; sops.sem_op = (short)1; sops.sem_flg = (short)0;
				semop(param_ptr->sem_data_id, &sops, 1);
			}
		}
	}
//------------------------------------------ Stop Sampling
	fclose(dumpfile_ptr);
	close(sock);
	param_ptr->validity &= (~ACTIVE);		// Set Sampling Activity Bit to 0

    return(0);
}
