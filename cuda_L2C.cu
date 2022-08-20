//	cuda_fft_xspec.c : FFT using CuFFT
//
//	Author : Seiji Kameno
//	Created: 2012/12/6
//
#include <cuda.h>
#include <cufft.h>
#include <string.h>
#include <math.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
// #include </usr/local/cuda/samples/common/inc/timer.h>
// #include <cuda_runtime.h>
// #include <timer.h>
#include "cuda_polaris.inc"
#define SCALEFACT 1.0/(NFFT* NsegSec)

int main(
	int		argc,			// Number of Arguments
	char	**argv )		// Pointer to Arguments
{
    int     rv, sd;                 // Return value from OCTAVIA2
    unsigned char *vdifhead_ptr;    // VDIF header
    unsigned char *vdifdata_ptr;    // VDIF data
    unsigned char buf[VDIF_SIZE];   // 1312 bytes
    struct sockaddr_in  addr_recv, addr_send;   // Socked address
    struct ip_mreq  mreq;           // Multicast request
    int     sock_recv, sock_send;   // Receive and Send Socket ID
    int     frameID, threadID;      // frame and thread ID
    int     MaxFrameIndex = 199999;
    int     index;
//------------------------------------------ Open sockets
    sock_recv = socket(AF_INET, SOCK_DGRAM, 0);
    sock_send = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock_recv < 0){
        perror("Socket (recv) Failed\n"); printf("%d\n", errno);
        return(-1);
    }
    if(sock_send < 0){
        perror("Socket (send) Failed\n"); printf("%d\n", errno);
        return(-1);
    }
    addr_recv.sin_family = AF_INET;
    addr_recv.sin_port   = htons(60000);
    addr_recv.sin_addr.s_addr    = INADDR_ANY;
    if( bind(sock_recv, (struct sockaddr *)&addr_recv, sizeof(addr_recv)) < 0){
        perror("Bind Failed (recv)\n"); printf("%d\n", errno);
    }
    addr_send.sin_family = AF_INET;
    addr_send.sin_port   = htons(60000);
    addr_send.sin_addr.s_addr    = htonl(INADDR_ANY);
    if( bind(sock_send, (struct sockaddr *)&addr_send, sizeof(addr_send)) < 0){
        perror("Bind Failed (send)\n"); printf("%d\n", errno);
    }
    memset(buf, 0, sizeof(buf));
//------------------------------------------ Receive VDIF
    frameID = 0;
    while( frameID < MaxFrameIndex ){
        rv = recv(sock_recv, buf, sizeof(buf), 0);
        frameID    = (buf[5] << 16) + (buf[6] << 8) + buf[7];
    }
    threadID    = ((buf[12] & 0x03) << 8 ) + buf[13] - 1;
    printf("frameID = %06d threadID = %d\n", frameID, threadID);
    while(threadID < 1){
        rv = recv(sock_recv, buf, sizeof(buf), 0);
        frameID    = (buf[5] << 16) + (buf[6] << 8) + buf[7];
        threadID    = ((buf[12] & 0x03) << 8 ) + buf[13] - 1;
        printf("frameID = %06d : threadID = %d\n", frameID, threadID);
    }
//------------------------------------------ Repeat
    printf("HIDOI\n");
    //while(1){
    frameID = 0;
    while(frameID < MaxFrameIndex){
        rv = recv(sock_recv, buf, sizeof(buf), 0);
        frameID    = (buf[5] << 16) + (buf[6] << 8) + buf[7];
        threadID   = ((buf[12] & 0x03) << 8 ) + buf[13] - 1;
        printf("frame%d  thread%d \r", frameID, threadID);
        sd = send(sock_send, buf, sizeof(buf), 0);
    }
    close(sock_recv);
    close(sock_send);
}
