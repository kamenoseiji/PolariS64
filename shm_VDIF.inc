#include <fcntl.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
//-------- Functions 
int valid_bit();
int usage();
int pow2round();
int shm_access();
long shm_init_create();
int erase_shm();

//-------- VDIF 
union VDIF32byte{
	int				int_buf[8];	
	unsigned char	chr_buf[32];
};

//-------- Shared Memory
#define	SEM_DATA_KEY	0x2000
#define	SHM_PARAM_KEY	0x1000
#define	VDIFHEAD_KEY 	0x0800
#define	VDIFDATA_KEY	0x0400
#define	XSPEC_KEY		0x0200
#define SHM_PARAM_FILE	"param.shm"	// File to record shared memory information
#define SHM_VDIFH_FILE	"vdifh.shm"	// File to record shared memory information
#define SHM_VDIFD_FILE	"vdifd.shm"	// File to record shared memory information
#define SHM_XSPEC_FILE	"xspec.shm"	// File to record shared memory information
#define SHM_FMT         "SHM_KEY(HEX)    SHM_ID     SHMSIZE(bytes)\n         %X        %d                %lu"
//-------- Shared Memory Size
// VDIF Buffer Memory Map
//         |<--- 0.25sec ->|
//         | page0 | page1 |
//         +-------+-------|
// thread0 | ST0P0 | ST0P1 |
// thread1 | ST1P0 | ST1P1 |
// thread2 | ST2P0 | ST2P1 |
// thread3 | ST3P0 | ST3P1 |
//  2 page / 0.25sec
//
#define SEM_NUM			16			// Number of Semaphores
#define	VDIF_SIZE		1312	    // 1312 bytes = Header (32 bytes) + Data (1280 bytes)
#define	VDIFHEAD_SIZE	32			// VDIF Header size
#define	VDIFDATA_SIZE	1280	    // VDIF data array size
//#define	MAX_SAMPLE_BUF	33554432	// 32 MB (1/4 sec for 1 Gbps)
//#define	HALFBUF			16777216	// 16 MB per 1/8 sec
#define PAGENUM			       8   // Number of pages
#define	NST			(param_ptr->num_st)		// Numver of streams
#define	NFFT		(param_ptr->segLen)		// FFT points
#define	NFFT2		(param_ptr->num_ch)		// Half of FFT points
#define	NFFTC		(param_ptr->num_ch + 1)	// NFFT/2+1 = Number of spectral points
#define	NsegSec		(param_ptr->segRate)	// # of segments per sec
#define	NsegPage	(param_ptr->segPage)	// # of segments per page
#define	MAX_NIF			16			// Maximum Number of IF
#define	Ncross			4			// Number of correlations <XX*>, <XY*>, <YX*>, <YY*>
#define	MAX_FILE_REC	3600		// Maximum number of records in output file
#define	POWER_TIME_NUM	512	    	// Maximum number of points to plot total power
#define	MAX_CH_VIEW	1024		// Maximum number of channels to plot

//-------- Shared Parameter
struct	SHM_PARAM{
	int		shrd_param_id;		// Shared Memory ID
	int		shrd_vdifhead_id;	// Shared Memory ID
	int		shrd_vdifdata_id;	// Shared Memory ID
	int		shrd_xspec_id;		// Shared Memory ID
	int		sem_data_id;		// Semaphore
	int		page_index;			// Index for page (0 or 1) of sampled data
	int		dummy;			    // Dummy
	int		dummy2;				// filler, reserved

	//-------- Sampling Modes
	int		sd_len;				// Size of 1-sec sampling data [bytes]
	int		num_st;				// Total number of streams (IFs)
	int		qbit;				// Quantization bits (1, 2, 4, or 8)
	int		fsample;			// Sampling Frequency [Hz]
	int		segLen;				// 1-segment length
	int		segRate;			// Number of segments in 1 sec
	int		segPage;			// Number of segments in 1 sec
	int		num_ch;				// Number of spectral channels

	//-------- Process ID
	int		pid_shm_alloc;		// Process ID
	int		pid_vdifsample;		// Process ID
	int		pid_segment;		// Process ID
	int		pid_fft;			// Process ID
	int		pdum1;
	int		pdum2;

	//-------- Status
	int		validity;			// Running / Ending signal
	int		AC_REC;				// Recording bits for Autocorrelation 
	int		XC_REC;				// Recording bits for Crosscorrelation 
	int		year;				// Year
	int		doy;				// Day of Year
	int		hour;				// Hour (UT)
	int		min;				// Minute
	int		sec;				// Second
	int		current_rec;		// current record index
	int		integ_rec;			// Total integration numer (MAX = 1800) 

	//-------- Total Power
	float	power[16];				// Power

};

//-------- Autocorrelation recording
#define P00_REC		0x00000001	// 0000 0000 0000 0000 0000 0000 0000 0001
#define P01_REC		0x00000002	// 0000 0000 0000 0000 0000 0000 0000 0010
#define P02_REC		0x00000004	// 0000 0000 0000 0000 0000 0000 0000 0100
#define P03_REC		0x00000008	// 0000 0000 0000 0000 0000 0000 0000 1000
#define P04_REC		0x00000010	// 0000 0000 0000 0000 0000 0000 0001 0000
#define P05_REC		0x00000020	// 0000 0000 0000 0000 0000 0000 0010 0000
#define P06_REC		0x00000040	// 0000 0000 0000 0000 0000 0000 0100 0000
#define P07_REC		0x00000080	// 0000 0000 0000 0000 0000 0000 1000 0000
#define P08_REC		0x00000100	// 0000 0000 0000 0000 0000 0001 0000 0000
#define P09_REC		0x00000200	// 0000 0000 0000 0000 0000 0010 0000 0000
#define P10_REC		0x00000400	// 0000 0000 0000 0000 0000 0100 0000 0000
#define P11_REC		0x00000800	// 0000 0000 0000 0000 0000 1000 0000 0000
#define P12_REC		0x00001000	// 0000 0000 0000 0000 0001 0000 0000 0000
#define P13_REC		0x00002000	// 0000 0000 0000 0000 0010 0000 0000 0000
#define P14_REC		0x00004000	// 0000 0000 0000 0000 0100 0000 0000 0000
#define P15_REC		0x00008000	// 0000 0000 0000 0000 1000 0000 0000 0000
#define A00_REC		0x00010000	// 0000 0000 0000 0001 0000 0000 0000 0000
#define A01_REC		0x00020000	// 0000 0000 0000 0010 0000 0000 0000 0000
#define A02_REC		0x00040000	// 0000 0000 0000 0100 0000 0000 0000 0000
#define A03_REC		0x00080000	// 0000 0000 0000 1000 0000 0000 0000 0000
#define A04_REC		0x00100000	// 0000 0000 0001 0000 0000 0000 0000 0000
#define A05_REC		0x00200000	// 0000 0000 0010 0000 0000 0000 0000 0000
#define A06_REC		0x00400000	// 0000 0000 0100 0000 0000 0000 0000 0000
#define A07_REC		0x00800000	// 0000 0000 1000 0000 0000 0000 0000 0000
#define A08_REC		0x01000000	// 0000 0001 0000 0000 0000 0000 0000 0000
#define A09_REC		0x02000000	// 0000 0010 0000 0000 0000 0000 0000 0000
#define A10_REC		0x04000000	// 0000 0100 0000 0000 0000 0000 0000 0000
#define A11_REC		0x08000000	// 0000 1000 0000 0000 0000 0000 0000 0000
#define A12_REC		0x10000000	// 0001 0000 0000 0000 0000 0000 0000 0000
#define A13_REC		0x20000000	// 0010 0000 0000 0000 0000 0000 0000 0000
#define A14_REC		0x40000000	// 0100 0000 0000 0000 0000 0000 0000 0000
#define A15_REC		0x80000000	// 1000 0000 0000 0000 0000 0000 0000 0000

//-------- Dictionary of validity bits
#define	UNDEF		0x00000000	// 0000 0000 0000 0000 0000 0000 0000 0000
#define	DISABLE		0x00000001	// 0000 0000 0000 0000 0000 0000 0000 0001
#define	ENABLE		0x00000002	// 0000 0000 0000 0000 0000 0000 0000 0010
#define	ACTIVE		0x00000004	// 0000 0000 0000 0000 0000 0000 0000 0100
#define	RESERVED	0x00000008	// 0000 0000 0000 0000 0000 0000 0000 1000
#define	DONE		0x00000010	// 0000 0000 0000 0000 0000 0000 0001 0000
#define	CONFIRMED	0x00000020	// 0000 0000 0000 0000 0000 0000 0010 0000
#define PGPLOT		0x00040000	// 0000 0000 0000 0100 0000 0000 0000 0000
#define SIMMODE		0x00080000	// 0000 0000 0000 1000 0000 0000 0000 0000
#define	FINISH		0x40000000	// 0100 0000 0000 0000 0000 0000 0000 0000
#define	ABSFIN		0x80000000	// 1000 0000 0000 0000 0000 0000 0000 0000

//-------- Semaphore Map
#define	SEM_VDIF_PART	0			// A/D for IF0, first half of every sec. 
#define	SEM_VDIF_POWER 	1			// A/D for IF1, first half of every sec. 
#define	SEM_SEG_F		8			// S-part bit->weight for first half
#define	SEM_SEG_L		9			// S-part bit->weight for first half
#define	SEM_FX			10			// FFT and Cross Corr. for every sec.
#define	SEM_POWER		11			// Total Power Measurement

//-------- Chiled Processes
#define SHM_PARM	"shm_param"
#define SHM_ALLOC	"shm_alloc"
#define VDIF_STORE	"VDIF_store"
#define VDIF_SIM	"VDIF_sim"
#define BITDIST		"bitDist"
#define CUDA_FFT	"cuda_fft_xspec"
#define SPEC_VIEW	"shm_spec_view"
#define POWER_VIEW	"shm_power_view"


//-------- Header Struct
struct vdifhead{
	unsigned int		I;			// Invalid data, 0=valid, 1=invalid
	unsigned int		ref_sec;	// Seconds from reference epoch
	unsigned int		ref_epoch;	// Reference Epoch, half-a-year since Y2K	
	unsigned int		frameID;	// frame ID within a second
	unsigned int		frameBytes;	// 1312 bytes per frame (fixed)
	unsigned int		qbit;		// Quantization bits -1
	unsigned int		thredID;	// D-port channel ID
	unsigned int		effBit;		// Effective bits
	unsigned int		sampleDiv;	// Sampling division in H or L frame
	unsigned int		split;		// frame splitting parameter
	unsigned int		TV;			// Time Valid
	unsigned int		ref_epochS;	// Reference epoch in source
	unsigned int		ref_secS;	// Second from reference epoch in source
	unsigned int		sec_count;	// 1PPS counter
};
