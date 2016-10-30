//	polaris_start.c : Start Polaris-Related Processes
//
//	Author : Seiji Kameno
//	Created: 2012/10/18
//
#include "shm_VDIF.inc"
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
int main(
	int		argc,			// Number of Arguments
	char	**argv )		// Pointer to Arguments
{
	int		ch_option;					// Option Charactors
	extern char	*optarg;				// Option Arguments
	extern int	optind, opterr;			// Option indicator and error
	int		shrd_param_id;				// Shared Memory ID
	struct	SHM_PARAM	*param_ptr;
	char	cmd[8][16];					// Command line arguments
	char	pgdev[16];					// PGPLOT Device
	char	dumpFname[256];				// File name to dump
	int		pid;						// Process ID
	int		integPP=0;					// Maximum record number [sec]
	int		bandWidth=16;				// Bandwidth [MHz]: 2/4/8/16/32/64/128/256/512/1024
	int		qbit=2;						// Quantization bits: 1/2/4/8
	int		num_st=16;					// Number of streams
	int		num_ch=32768;				// Number of spectral channels: 2^n (n=9..18)
	int		ARecFlag   = 0;				// Autocorr Recording Flag
	int		CRecFlag   = 0;				// Autocorr Recording Flag
	int		statusFlag = 0;				// Used for option parser
	int		dumpFlag   = 0;				// Dump samples into a file
//------------------------------------------ Get Path
	FILE	*file_ptr;
	char	path_dir[256];
	char	path_str[256];
	if( (file_ptr = popen("which shm_param", "r")) == NULL){
		perror("No active path to shm_param!!\n"); return(-1);
	}
	while( fgets(path_dir, 256, file_ptr) != NULL){
		path_dir[strlen(path_dir) - 10] = 0x00;
	}
	pclose(file_ptr);
//------------------------------------------ Option Parser
	while(( ch_option = getopt(argc, argv, "a:b:c:d:f:hi:n:p:q:s:Sv:")) != -1){
		switch(ch_option){
			case 'a':	ARecFlag |= ((valid_bit(optarg) & 0xffff) <<  16);	break;
			case 'b':	bandWidth = atoi(optarg);	break;
			case 'c':	CRecFlag |= (valid_bit(optarg) & 0xffff);	break;
			case 'd':	dumpFlag |= 0x01; strcpy(dumpFname, optarg);	break;
			case 'h':	usage();	return(0);
			case 'i':	integPP = atoi(optarg);	break;
			case 'n':	num_st = atoi(optarg);	break;
			case 'p':	ARecFlag |= (valid_bit(optarg) & 0xffff);	break;
			case 'q':	qbit = pow2round(atoi(optarg));	break;
			case 's':	num_ch = pow2round(atoi(optarg));	break;
			case 'S':	statusFlag |= SIMMODE; strcpy(dumpFname, optarg); break;
			case 'v':	statusFlag |= PGPLOT; strcpy(pgdev, optarg);	break;
		}	
	}
//------------------------------------------ Start shm_param()
	if( fork() == 0){
		pid = getpid(); sprintf(cmd[0], SHM_PARM);
		sprintf(path_str, "%s%s", path_dir, SHM_PARM);
		printf(" Exec %s as Chiled Process [PID = %d]\n", cmd[0], pid);
		if( execl( path_str, cmd[0], (char *)NULL ) == -1){
			perror("Can't Create Chiled Proces!!\n"); return(-1);
		}
	}
//------------------------------------------ Access to the shared parameter
	sleep(1);		// Wait for 1 sec until Shared memory will be ready
	shrd_param_id = shmget( SHM_PARAM_KEY, sizeof(struct SHM_PARAM), 0444);
	param_ptr  = (struct SHM_PARAM *)shmat(shrd_param_id, NULL, 0);
	param_ptr->pid_shm_alloc = pid;
//------------------------------------------ Set the validity bits
	param_ptr->validity |= statusFlag;
	param_ptr->AC_REC   |= ARecFlag;
	param_ptr->XC_REC   |= CRecFlag;
	param_ptr->integ_rec = integPP;		// Duration of spectroscopy [sec]
	param_ptr->qbit 	 = qbit;		// Quantization bits
	param_ptr->segLen    = num_ch* 2;	// FFT segment length
	param_ptr->num_st    = num_st;		// Number of streams
	param_ptr->num_ch    = num_ch;		// Number of spectral channels
	param_ptr->fsample   = pow2round(2* bandWidth)* 1000000;	// Sampling freq.
	param_ptr->segNum    = pow2round((unsigned int)(param_ptr->fsample / param_ptr->num_ch));// Number of segments in 1 sec
	printf("AREC FLAG = %X \n", param_ptr->AC_REC);
	printf("%d Segments per sec\n", param_ptr->segNum);
	printf("%d Segments per part\n", NsegPart);
//------------------------------------------ Start shm_alloc()
	if( fork() == 0){
		pid = getpid(); sprintf(cmd[0], SHM_ALLOC);
		sprintf(path_str, "%s%s", path_dir, SHM_ALLOC);
		printf("%s\n", path_str);
		printf(" Exec %s as Chiled Process [PID = %d]\n", cmd[0], pid);
		if( execl( path_str, cmd[0], (char *)NULL ) == -1){
			perror("Can't Create Chiled Proces!!\n"); return(-1);
		}
	}
	sleep(1);	// Wait 1 sec until shared memory will be ready
//------------------------------------------ Start acquiring VDIF data
	if( fork() == 0){
		//-------- Simulation Mode
		if( param_ptr->validity & SIMMODE ){
			strcpy(cmd[1], dumpFname);
			pid = getpid(); sprintf(cmd[0], VDIF_SIM);
			sprintf(path_str, "%s%s", path_dir, VDIF_SIM);
			printf(" Exec %s as Chiled Process [PID = %d]\n", cmd[0], pid);
			if( execl( path_str, cmd[0], cmd[1], (char *)NULL ) == -1){
				perror("Can't Create Chiled Proces!!\n"); return(-1);
			}
		} else {
		//-------- Real Mode
			pid = getpid(); sprintf(cmd[0], VDIF_STORE);
			sprintf(path_str, "%s%s", path_dir, VDIF_STORE);
			printf(" Exec %s as Chiled Process [PID = %d]\n", cmd[0], pid);
			if(dumpFlag){
				strcpy(cmd[1], dumpFname);
				if( execl( path_str, cmd[0], cmd[1], (char *)NULL ) == -1){
					perror("Can't Create Chiled Proces!!\n"); return(-1);
				}
			} else {
				if( execl( path_str, cmd[0], (char *)NULL ) == -1){
					perror("Can't Create Chiled Proces!!\n"); return(-1);
				}
			}
		}
	}
    #ifdef HIDOI
//------------------------------------------ Start Spectrum Viewer
	if( param_ptr->validity & PGPLOT ){
		strcpy(cmd[1], pgdev);
		if( fork() == 0){
			pid = getpid(); sprintf(cmd[0], POWER_VIEW);
			sprintf(path_str, "%s%s", path_dir, POWER_VIEW);
			printf(" Exec %s as Chiled Process [PID = %d]\n", cmd[0], pid);
			if( execl( path_str, cmd[0], cmd[1], (char *)NULL ) == -1){
				perror("Can't Create Chiled Proces!!\n"); return(-1);
			}
		}
		if( fork() == 0){
			pid = getpid(); sprintf(cmd[0], SPEC_VIEW);
			sprintf(path_str, "%s%s", path_dir, SPEC_VIEW);
			printf(" Exec %s as Chiled Process [PID = %d]\n", cmd[0], pid);
			if( execl( path_str, cmd[0], cmd[1], (char *)NULL ) == -1){
				perror("Can't Create Chiled Proces!!\n"); return(-1);
			}
		}
	}
//------------------------------------------ Start CUDA FFT
	sleep(1);		// Wait 1 sec
	if( fork() == 0){
		pid = getpid(); sprintf(cmd[0], CUDA_FFT);
		sprintf(path_str, "%s%s", path_dir, CUDA_FFT);
		printf(" Exec %s as Chiled Process [PID = %d]\n", cmd[0], pid);
		if( execl( path_str, cmd[0], (char *)NULL ) == -1){
			perror("Can't Create Chiled Proces!!\n"); return(-1);
		}
	}
	if( fork() == 0){
		pid = getpid(); sprintf(cmd[0], BITDIST);
		sprintf(path_str, "%s%s", path_dir, BITDIST);
		printf(" Exec %s as Chiled Process [PID = %d]\n", cmd[0], pid);
		if( execl( path_str, cmd[0], (char *)NULL ) == -1){
			perror("Can't Create Chiled Proces!!\n"); return(-1);
		}
	}
    #endif
    return(0);
}

int usage(){
	fprintf(stderr, "USAGE: polaris_start [-chipv] \n");
	fprintf(stderr, "  -a : Specify autocorrelation files to save.\n");
	fprintf(stderr, "       0 -> CH0 is recorded, 3C -> CH3 and CH12 are recorded. Default: no autocorr, recorded. \n");
	fprintf(stderr, "  -b : Specify bandwidth [MHz] for each IF. Default: 8 MHz.\n");
	fprintf(stderr, "  -c : Specify crosscorrelation files not saved.\n");
	fprintf(stderr, "       0 -> CH0xCH2 is recorded, 01 -> all Xcorrs are recorded. Default: no xcorr, recoreded. \n");
	fprintf(stderr, "  -d : Dump sampling data into a file specified following the option.\n");
	fprintf(stderr, "  -h : Show help \n");
	fprintf(stderr, "  -i : Recording time [sec]. Unless specified, polaris keep recordeng until shm_init.\n"); 
	fprintf(stderr, "  -n : Number of streams.\n"); 
	fprintf(stderr, "  -p : Specify bit-distibution files to save. Index is the same with -a option.\n");
	fprintf(stderr, "  -q : Specify quantization bits. Default: 4 bit.\n");
	fprintf(stderr, "  -s : Specify number of spectral channels (2^n).\n");
	fprintf(stderr, "  -S : Simulation mode (read /DATA/VERA7.data instead of OCTAVIA).\n");
	fprintf(stderr, "  -v : Specify PGPLOT window to display spectra. /xw -> X window, /gif -> GIF, /null -> no view.\n");
	return(0);
}

int valid_bit( char *option ){
	int		valid = 0;
	int		index;
	char	option_value[16] = "0123456789ABCDEF";
	for( index=0; index<16; index++){
		if(strchr( option, option_value[index] ) != NULL)	valid |= (0x01 << index);
	}
	// if(strstr( option, "0" ) != NULL)	valid |= P00_REC;
	// if(strstr( option, "1" ) != NULL)	valid |= P01_REC;
	// if(strstr( option, "2" ) != NULL)	valid |= P02_REC;
	// if(strstr( option, "3" ) != NULL)	valid |= P03_REC;
	return(valid);
}
