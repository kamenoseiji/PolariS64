//	cuda_fft_xspec.c : FFT using CuFFT
//
//	Author : Seiji Kameno
//	Created: 2012/12/6
//
#include "shm_VDIF.inc"
#include <cuda.h>
#include <cufft.h>

__device__ float2 complexMult(float2 a, float2 b)		// output a x b
{
	return make_float2( a.x* b.x - a.y* b.y, a.x* b.y + a.y* b.x );
}

__device__ float2 complexMultConj(float2 a, float2 b)		// output a x b*
{
	return make_float2( a.x* b.x + a.y* b.y,  a.y* b.x - a.x* b.y );
}

__device__ float complexMod( float2 a )				// output |a|^2
{
	return  a.x* a.x + a.y* a.y;
}

__global__ void complexMultConjVec(		// calculate a x b*
	float2	*vec_in_a,			// Input vector
	float2	*vec_in_b,			// Input vector
	float2	*vec_out,			// Output vector
	int		length)				// Vector length
{
	int tid = blockIdx.x* blockDim.x + threadIdx.x;
	if((tid >= 0) && (tid < length)){
		vec_out[tid] = complexMultConj(vec_in_a[tid], vec_in_b[tid]);
	}
}

__global__ void complexPowerVec(		// calculate a x a*
	float2	*vec_in,		// Input vector
	float	*vec_out,		// Output vector
	int		length)			// Number of elements
{
	int tid = blockIdx.x* blockDim.x + threadIdx.x;
	if((tid >= 0) && (tid < length)){
		vec_out[tid] = complexMod(vec_in[tid]);
	}
}

__global__ void accumReal(	// a <- a + b
	float	*vec_in_a,		// Accumuration Results
	float	*vec_in_b,		// to be accumulated
	int		length)
{
    int tid = blockIdx.x* blockDim.x + threadIdx.x;
    if((tid >= 0) && (tid < length)){
        vec_in_a[tid] += vec_in_b[tid];
    }
}

__global__ void accumComplex(	// a <- a + b
	float2	*vec_in_a,		// Accumuration Results
	float2	*vec_in_b,		// to be accumulated
	int		length)
{
    int tid = blockIdx.x* blockDim.x + threadIdx.x;
    if((tid >= 0) && (tid < length)){
        vec_in_a[tid].x += vec_in_b[tid].x;
        vec_in_a[tid].y += vec_in_b[tid].y;
    }
}

__global__ void accumPowerSpec(
	float2	*vec_in,		// Input vector to be accumulated
	float	*vec_out,		// In/Output vector to accumulate
	int		length)			// Length to accumulating vector
{
    int ix = blockIdx.x* blockDim.x + threadIdx.x;

	if( (ix >= 0) && (ix < length) ){
		vec_out[ix] += vec_in[ix].x *  vec_in[ix].x +  vec_in[ix].y *  vec_in[ix].y;
	}
}

__global__ void accumCrossSpec(
	float2	*vec_in_a,		// Input vector
	float2	*vec_in_b,		// Input vector
	float2	*vec_out,		// Output vector
	int		length)
{
    int ix = blockIdx.x* blockDim.x + threadIdx.x;

	if( (ix >= 0) && (ix < length) ){
		vec_out[ix].x += vec_in_a[ix].x * vec_in_b[ix].x + vec_in_a[ix].y * vec_in_b[ix].y;
		vec_out[ix].y += vec_in_a[ix].y * vec_in_b[ix].x - vec_in_a[ix].x * vec_in_b[ix].y;
	}
}

__global__ void scalePowerSpec(
	float	*vec_in,		// Input vector to be accumulated
	float	scaleFact,		// Scaling Factor
	int		length)			// Length to accumulating vector
{
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < length) ){
		 vec_in[ix] *= scaleFact;
	}
}

__global__ void scaleCrossSpec(
	float2	*vec_in,		// Input vector to be accumulated
	float	scaleFact,		// Scaling Factor
	int		length)			// Length to accumulating vector
{
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < length) ){
		 vec_in[ix].x *= scaleFact;
		 vec_in[ix].y *= scaleFact;
	}
}

//-------- Format segment data using 4-bit quantized K5 data
__global__ void segform4bit(
	unsigned char	*k5data_in,	// Input K5 Segmanet Data
	float	*segdata,			// Output Segment Data
	int		length)				// Length to copy
{
	float	bias = 7.5;			// Mean value
	float	scale = 0.0625;		// Scaling by 1/16
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	unsigned char bitmask = 0x0f;				// 4-bit mask
	
	if( (ix >= 0) && (ix < length) ){
		segdata[           ix] = ((float)((k5data_in[2*ix])   & bitmask) - bias)* scale;
		segdata[  length + ix] = ((float)((k5data_in[2*ix] >>  4)      ) - bias)* scale;
		segdata[2*length + ix] = ((float)((k5data_in[2*ix+1]) & bitmask) - bias)* scale;
		segdata[3*length + ix] = ((float)((k5data_in[2*ix+1]>> 4)      ) - bias)* scale;
	}
}

//-------- Format segment data using 8-bit quantized K5 data
__global__ void segform8bit(
	unsigned char	*k5data_in,		// Input K5 Segmanet Data
	float	*segdata,		// Output Segment Data
	int		length)			// Length to copy
{
	float	bias = 127.5;		// Mean value
	float	scale = 0.00390625;	// Scaling by 1/256
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < length) ){
		segdata[           ix] = ((float)k5data_in[4*ix]  - bias)*scale;
		segdata[  length + ix] = ((float)k5data_in[4*ix+1]- bias)*scale;
		segdata[2*length + ix] = ((float)k5data_in[4*ix+2]- bias)*scale;
		segdata[3*length + ix] = ((float)k5data_in[4*ix+3]- bias)*scale;
	}
}
//-------- Format segment data using 1-stream 2-bit (VLBI1)
__global__ void segform_1st_2bit(
	unsigned char   *vdifdata_in,		// Input VDIF Data, 1-byte packed (for 2 samples)
	float   *segdata,					// Output Segment Data
	int		length)						// Length to copy
{
	int	hex_length = length >> 2;
	float	bias = 1.5;
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < hex_length) ){
		segdata[4* ix     ] = (float)(((vdifdata_in[ix   ] >> 7) & 0x01) | ((vdifdata_in[ix   ] >> 5) & 0x02) ) - bias ;
		segdata[4* ix +  1] = (float)(((vdifdata_in[ix   ] >> 5) & 0x01) | ((vdifdata_in[ix   ] >> 3) & 0x02) ) - bias ;
		segdata[4* ix +  2] = (float)(((vdifdata_in[ix   ] >> 3) & 0x01) | ((vdifdata_in[ix   ] >> 1) & 0x02) ) - bias ;
		segdata[4* ix +  3] = (float)(((vdifdata_in[ix   ] >> 1) & 0x01) | ((vdifdata_in[ix   ] << 1) & 0x02) ) - bias ;
	}
}
//-------- Format segment data using 2-stream 2-bit (VERA1)
__global__ void segform_2st_2bit(
	unsigned char   *vdifdata_in,		// Input VDIF Data, 1-byte packed (for 2 samples)
	float   *segdata,					// Output Segment Data
	int		length)						// Length to copy
{
	int	oct_length = length/8;
	// unsigned char bitmask = 0x03;		// 2-bit mask
	int ix8, ix4;
	float	bias = 1.5;
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < oct_length) ){
		ix4 = 4* ix; ix8 = 2* ix4;
		segdata[         ix8    ] = (float)(((vdifdata_in[ix4   ] >> 7) & 0x01) | ((vdifdata_in[ix4   ] >> 5) & 0x02) ) - bias ;
		segdata[         ix8 + 1] = (float)(((vdifdata_in[ix4   ] >> 5) & 0x01) | ((vdifdata_in[ix4   ] >> 3) & 0x02) ) - bias ;
		segdata[         ix8 + 2] = (float)(((vdifdata_in[ix4   ] >> 3) & 0x01) | ((vdifdata_in[ix4   ] >> 1) & 0x02) ) - bias ;
		segdata[         ix8 + 3] = (float)(((vdifdata_in[ix4   ] >> 1) & 0x01) | ((vdifdata_in[ix4   ] << 1) & 0x02) ) - bias ;
		segdata[         ix8 + 4] = (float)(((vdifdata_in[ix4 +1] >> 7) & 0x01) | ((vdifdata_in[ix4 +1] >> 5) & 0x02) ) - bias ;
		segdata[         ix8 + 5] = (float)(((vdifdata_in[ix4 +1] >> 5) & 0x01) | ((vdifdata_in[ix4 +1] >> 3) & 0x02) ) - bias ;
		segdata[         ix8 + 6] = (float)(((vdifdata_in[ix4 +1] >> 3) & 0x01) | ((vdifdata_in[ix4 +1] >> 1) & 0x02) ) - bias ;
		segdata[         ix8 + 7] = (float)(((vdifdata_in[ix4 +1] >> 1) & 0x01) | ((vdifdata_in[ix4 +1] << 1) & 0x02) ) - bias ;
		segdata[length + ix8    ] = (float)(((vdifdata_in[ix4 +2] >> 7) & 0x01) | ((vdifdata_in[ix4 +2] >> 5) & 0x02) ) - bias ;
		segdata[length + ix8 + 1] = (float)(((vdifdata_in[ix4 +2] >> 5) & 0x01) | ((vdifdata_in[ix4 +2] >> 3) & 0x02) ) - bias ;
		segdata[length + ix8 + 2] = (float)(((vdifdata_in[ix4 +2] >> 3) & 0x01) | ((vdifdata_in[ix4 +2] >> 1) & 0x02) ) - bias ;
		segdata[length + ix8 + 3] = (float)(((vdifdata_in[ix4 +2] >> 1) & 0x01) | ((vdifdata_in[ix4 +2] << 1) & 0x02) ) - bias ;
		segdata[length + ix8 + 4] = (float)(((vdifdata_in[ix4 +3] >> 7) & 0x01) | ((vdifdata_in[ix4 +3] >> 5) & 0x02) ) - bias ;
		segdata[length + ix8 + 5] = (float)(((vdifdata_in[ix4 +3] >> 5) & 0x01) | ((vdifdata_in[ix4 +3] >> 3) & 0x02) ) - bias ;
		segdata[length + ix8 + 6] = (float)(((vdifdata_in[ix4 +3] >> 3) & 0x01) | ((vdifdata_in[ix4 +3] >> 1) & 0x02) ) - bias ;
		segdata[length + ix8 + 7] = (float)(((vdifdata_in[ix4 +3] >> 1) & 0x01) | ((vdifdata_in[ix4 +3] << 1) & 0x02) ) - bias ;
		//
		// segdata[         8* ix    ] = weight[(vdifdata_in[4*ix   ] >> 6) & bitmask];	// Stream 0
		// segdata[         8* ix + 1] = weight[(vdifdata_in[4*ix   ] >> 4) & bitmask];	// Stream 0
		// segdata[         8* ix + 2] = weight[(vdifdata_in[4*ix   ] >> 2) & bitmask];	// Stream 0
		// segdata[         8* ix + 3] = weight[(vdifdata_in[4*ix   ]     ) & bitmask];	// Stream 0
		// segdata[         8* ix + 4] = weight[(vdifdata_in[4*ix +1] >> 6) & bitmask];	// Stream 0
		// segdata[         8* ix + 5] = weight[(vdifdata_in[4*ix +1] >> 4) & bitmask];	// Stream 0
		// segdata[         8* ix + 6] = weight[(vdifdata_in[4*ix +1] >> 2) & bitmask];	// Stream 0
		// segdata[         8* ix + 7] = weight[(vdifdata_in[4*ix +1]     ) & bitmask];	// Stream 0
		// segdata[length + 8* ix    ] = weight[(vdifdata_in[4*ix +2] >> 6) & bitmask];	// Stream 1
		// segdata[length + 8* ix + 1] = weight[(vdifdata_in[4*ix +2] >> 4) & bitmask];	// Stream 1
		// segdata[length + 8* ix + 2] = weight[(vdifdata_in[4*ix +2] >> 2) & bitmask];	// Stream 1
		// segdata[length + 8* ix + 3] = weight[(vdifdata_in[4*ix +2]     ) & bitmask];	// Stream 1
		// segdata[length + 8* ix + 4] = weight[(vdifdata_in[4*ix +3] >> 6) & bitmask];	// Stream 1
		// segdata[length + 8* ix + 5] = weight[(vdifdata_in[4*ix +3] >> 4) & bitmask];	// Stream 1
		// segdata[length + 8* ix + 6] = weight[(vdifdata_in[4*ix +3] >> 2) & bitmask];	// Stream 1
		// segdata[length + 8* ix + 7] = weight[(vdifdata_in[4*ix +3]     ) & bitmask];	// Stream 1
	}
}
//-------- Format segment data using 4-stream 2-bit (VERA2)
__global__ void segform_4st_2bit(
	unsigned char   *vdifdata_in,		// Input VDIF Data, 1-byte packed (for 2 samples)
	float   *segdata,					// Output Segment Data
	int		length)						// Length to copy
{
	int	quad_length = length/4;
	// unsigned char bitmask = 0x03;		// 2-bit mask
	float	bias = 1.5;
	// float	weight[] = {-3.0, 1.0, -1.0, 3.0};
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < quad_length) ){
		segdata[            4* ix    ] = (float)(((vdifdata_in[4*ix   ] >> 7) & 0x01) | ((vdifdata_in[4*ix   ] >> 5) & 0x02) ) - bias ;
		segdata[            4* ix + 1] = (float)(((vdifdata_in[4*ix   ] >> 5) & 0x01) | ((vdifdata_in[4*ix   ] >> 3) & 0x02) ) - bias ;
		segdata[            4* ix + 2] = (float)(((vdifdata_in[4*ix   ] >> 3) & 0x01) | ((vdifdata_in[4*ix   ] >> 1) & 0x02) ) - bias ;
		segdata[            4* ix + 3] = (float)(((vdifdata_in[4*ix   ] >> 1) & 0x01) | ((vdifdata_in[4*ix   ] << 1) & 0x02) ) - bias ;
		segdata[   length + 4* ix    ] = (float)(((vdifdata_in[4*ix +1] >> 7) & 0x01) | ((vdifdata_in[4*ix +1] >> 5) & 0x02) ) - bias ;
		segdata[   length + 4* ix + 1] = (float)(((vdifdata_in[4*ix +1] >> 5) & 0x01) | ((vdifdata_in[4*ix +1] >> 3) & 0x02) ) - bias ;
		segdata[   length + 4* ix + 2] = (float)(((vdifdata_in[4*ix +1] >> 3) & 0x01) | ((vdifdata_in[4*ix +1] >> 1) & 0x02) ) - bias ;
		segdata[   length + 4* ix + 3] = (float)(((vdifdata_in[4*ix +1] >> 1) & 0x01) | ((vdifdata_in[4*ix +1] << 1) & 0x02) ) - bias ;
		segdata[2* length + 4* ix    ] = (float)(((vdifdata_in[4*ix +2] >> 7) & 0x01) | ((vdifdata_in[4*ix +2] >> 5) & 0x02) ) - bias ;
		segdata[2* length + 4* ix + 1] = (float)(((vdifdata_in[4*ix +2] >> 5) & 0x01) | ((vdifdata_in[4*ix +2] >> 3) & 0x02) ) - bias ;
		segdata[2* length + 4* ix + 2] = (float)(((vdifdata_in[4*ix +2] >> 3) & 0x01) | ((vdifdata_in[4*ix +2] >> 1) & 0x02) ) - bias ;
		segdata[2* length + 4* ix + 3] = (float)(((vdifdata_in[4*ix +2] >> 1) & 0x01) | ((vdifdata_in[4*ix +2] << 1) & 0x02) ) - bias ;
		segdata[3* length + 4* ix    ] = (float)(((vdifdata_in[4*ix +3] >> 7) & 0x01) | ((vdifdata_in[4*ix +3] >> 5) & 0x02) ) - bias ;
		segdata[3* length + 4* ix + 1] = (float)(((vdifdata_in[4*ix +3] >> 5) & 0x01) | ((vdifdata_in[4*ix +3] >> 3) & 0x02) ) - bias ;
		segdata[3* length + 4* ix + 2] = (float)(((vdifdata_in[4*ix +3] >> 3) & 0x01) | ((vdifdata_in[4*ix +3] >> 1) & 0x02) ) - bias ;
		segdata[3* length + 4* ix + 3] = (float)(((vdifdata_in[4*ix +3] >> 1) & 0x01) | ((vdifdata_in[4*ix +3] << 1) & 0x02) ) - bias ;
		// segdata[            4* ix    ] = weight[(vdifdata_in[4*ix   ] >> 6) & bitmask];	// Stream 0
		// segdata[            4* ix + 1] = weight[(vdifdata_in[4*ix   ] >> 4) & bitmask];	// Stream 0
		// segdata[            4* ix + 2] = weight[(vdifdata_in[4*ix   ] >> 2) & bitmask];	// Stream 0
		// segdata[            4* ix + 3] = weight[(vdifdata_in[4*ix   ]     ) & bitmask];	// Stream 0
		// segdata[   length + 4* ix    ] = weight[(vdifdata_in[4*ix +1] >> 6) & bitmask];	// Stream 1
		// segdata[   length + 4* ix + 1] = weight[(vdifdata_in[4*ix +1] >> 4) & bitmask];	// Stream 1
		// segdata[   length + 4* ix + 2] = weight[(vdifdata_in[4*ix +1] >> 2) & bitmask];	// Stream 1
		// segdata[   length + 4* ix + 3] = weight[(vdifdata_in[4*ix +1]     ) & bitmask];	// Stream 1
		// segdata[2* length + 4* ix    ] = weight[(vdifdata_in[4*ix +2] >> 6) & bitmask];	// Stream 2
		// segdata[2* length + 4* ix + 1] = weight[(vdifdata_in[4*ix +2] >> 4) & bitmask];	// Stream 2
		// segdata[2* length + 4* ix + 2] = weight[(vdifdata_in[4*ix +2] >> 2) & bitmask];	// Stream 2
		// segdata[2* length + 4* ix + 3] = weight[(vdifdata_in[4*ix +2]     ) & bitmask];	// Stream 2
		// segdata[3* length + 4* ix    ] = weight[(vdifdata_in[4*ix +3] >> 6) & bitmask];	// Stream 3
		// segdata[3* length + 4* ix + 1] = weight[(vdifdata_in[4*ix +3] >> 4) & bitmask];	// Stream 3
		// segdata[3* length + 4* ix + 2] = weight[(vdifdata_in[4*ix +3] >> 2) & bitmask];	// Stream 3
		// segdata[3* length + 4* ix + 3] = weight[(vdifdata_in[4*ix +3]     ) & bitmask];	// Stream 3
	}
}

//-------- Format segment data using 8-stream 2-bit (VERA4)
__global__ void segform_8st_2bit(
	unsigned char   *vdifdata_in,		// Input VDIF Data, 2-byte packed (for 1 samples)
	float   *segdata,					// Output Segment Data
	int		length)						// Length to copy
{
	int	half_length = length/2;
	// unsigned char bitmask = 0x03;		// 2-bit mask
	float	bias = 1.5;
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < half_length) ){
		segdata[            2* ix    ] = (float)(((vdifdata_in[4*ix   ] >> 7) & 0x01) | ((vdifdata_in[4*ix   ] >> 5) & 0x02) ) - bias ;
		segdata[            2* ix + 1] = (float)(((vdifdata_in[4*ix   ] >> 5) & 0x01) | ((vdifdata_in[4*ix   ] >> 3) & 0x02) ) - bias ;
		segdata[   length + 2* ix    ] = (float)(((vdifdata_in[4*ix   ] >> 3) & 0x01) | ((vdifdata_in[4*ix   ] >> 1) & 0x02) ) - bias ;
		segdata[   length + 2* ix + 1] = (float)(((vdifdata_in[4*ix   ] >> 1) & 0x01) | ((vdifdata_in[4*ix   ] << 1) & 0x02) ) - bias ;
		segdata[2* length + 2* ix    ] = (float)(((vdifdata_in[4*ix +1] >> 7) & 0x01) | ((vdifdata_in[4*ix +1] >> 5) & 0x02) ) - bias ;
		segdata[2* length + 2* ix + 1] = (float)(((vdifdata_in[4*ix +1] >> 5) & 0x01) | ((vdifdata_in[4*ix +1] >> 3) & 0x02) ) - bias ;
		segdata[3* length + 2* ix    ] = (float)(((vdifdata_in[4*ix +1] >> 3) & 0x01) | ((vdifdata_in[4*ix +1] >> 1) & 0x02) ) - bias ;
		segdata[3* length + 2* ix + 1] = (float)(((vdifdata_in[4*ix +1] >> 1) & 0x01) | ((vdifdata_in[4*ix +1] << 1) & 0x02) ) - bias ;
		segdata[4* length + 2* ix    ] = (float)(((vdifdata_in[4*ix +2] >> 7) & 0x01) | ((vdifdata_in[4*ix +2] >> 5) & 0x02) ) - bias ;
		segdata[4* length + 2* ix + 1] = (float)(((vdifdata_in[4*ix +2] >> 5) & 0x01) | ((vdifdata_in[4*ix +2] >> 3) & 0x02) ) - bias ;
		segdata[5* length + 2* ix    ] = (float)(((vdifdata_in[4*ix +2] >> 3) & 0x01) | ((vdifdata_in[4*ix +2] >> 1) & 0x02) ) - bias ;
		segdata[5* length + 2* ix + 1] = (float)(((vdifdata_in[4*ix +2] >> 1) & 0x01) | ((vdifdata_in[4*ix +2] << 1) & 0x02) ) - bias ;
		segdata[6* length + 2* ix    ] = (float)(((vdifdata_in[4*ix +3] >> 7) & 0x01) | ((vdifdata_in[4*ix +3] >> 5) & 0x02) ) - bias ;
		segdata[6* length + 2* ix + 1] = (float)(((vdifdata_in[4*ix +3] >> 5) & 0x01) | ((vdifdata_in[4*ix +3] >> 3) & 0x02) ) - bias ;
		segdata[7* length + 2* ix    ] = (float)(((vdifdata_in[4*ix +3] >> 3) & 0x01) | ((vdifdata_in[4*ix +3] >> 1) & 0x02) ) - bias ;
		segdata[7* length + 2* ix + 1] = (float)(((vdifdata_in[4*ix +3] >> 1) & 0x01) | ((vdifdata_in[4*ix +3] << 1) & 0x02) ) - bias ;
		// segdata[             2* ix    ] = weight[(vdifdata_in[4*ix   ] >> 6) & bitmask];		// Stream 0
		// segdata[             2* ix + 1] = weight[(vdifdata_in[4*ix   ] >> 4) & bitmask];		// Stream 0
		// segdata[    length + 2* ix    ] = weight[(vdifdata_in[4*ix   ] >> 2) & bitmask];		// Stream 1
		// segdata[    length + 2* ix + 1] = weight[(vdifdata_in[4*ix   ]     ) & bitmask];		// Stream 1
		// segdata[ 2* length + 2* ix    ] = weight[(vdifdata_in[4*ix +1] >> 6) & bitmask];		// Stream 2
		// segdata[ 2* length + 2* ix + 1] = weight[(vdifdata_in[4*ix +1] >> 4) & bitmask];		// Stream 2
		// segdata[ 3* length + 2* ix    ] = weight[(vdifdata_in[4*ix +1] >> 2) & bitmask];		// Stream 3
		// segdata[ 3* length + 2* ix + 1] = weight[(vdifdata_in[4*ix +1]     ) & bitmask];		// Stream 3
		// segdata[ 4* length + 2* ix    ] = weight[(vdifdata_in[4*ix +2] >> 6) & bitmask];		// Stream 4
		// segdata[ 4* length + 2* ix + 1] = weight[(vdifdata_in[4*ix +2] >> 4) & bitmask];		// Stream 4
		// segdata[ 5* length + 2* ix    ] = weight[(vdifdata_in[4*ix +2] >> 2) & bitmask];		// Stream 5
		// segdata[ 5* length + 2* ix + 1] = weight[(vdifdata_in[4*ix +2]     ) & bitmask];		// Stream 5
		// segdata[ 6* length + 2* ix    ] = weight[(vdifdata_in[4*ix +3] >> 6) & bitmask];		// Stream 6
		// segdata[ 6* length + 2* ix + 1] = weight[(vdifdata_in[4*ix +3] >> 4) & bitmask];		// Stream 6
		// segdata[ 7* length + 2* ix    ] = weight[(vdifdata_in[4*ix +3] >> 2) & bitmask];		// Stream 7
		// segdata[ 7* length + 2* ix + 1] = weight[(vdifdata_in[4*ix +3]     ) & bitmask];		// Stream 7
	}
}

//-------- Format segment data using 16-stream 2-bit (VERA7)
__global__ void segform_16st_2bit(
	unsigned char   *vdifdata_in,		// Input VDIF Data, 4-byte packed (for 1 samples)
	float   *segdata,					// Output Segment Data
	int		length)						// Length to copy
{
	// unsigned char bitmask = 0x03;		// 2-bit mask
	float	bias = 1.5;
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < length) ){
		segdata[             ix] = (float)(((vdifdata_in[4*ix   ] >> 7) & 0x01) | ((vdifdata_in[4*ix   ] >> 5) & 0x02) ) - bias ;
		segdata[    length + ix] = (float)(((vdifdata_in[4*ix   ] >> 5) & 0x01) | ((vdifdata_in[4*ix   ] >> 3) & 0x02) ) - bias ;
		segdata[ 2* length + ix] = (float)(((vdifdata_in[4*ix   ] >> 3) & 0x01) | ((vdifdata_in[4*ix   ] >> 1) & 0x02) ) - bias ;
		segdata[ 3* length + ix] = (float)(((vdifdata_in[4*ix   ] >> 1) & 0x01) | ((vdifdata_in[4*ix   ] << 1) & 0x02) ) - bias ;
		segdata[ 4* length + ix] = (float)(((vdifdata_in[4*ix +1] >> 7) & 0x01) | ((vdifdata_in[4*ix +1] >> 5) & 0x02) ) - bias ;
		segdata[ 5* length + ix] = (float)(((vdifdata_in[4*ix +1] >> 5) & 0x01) | ((vdifdata_in[4*ix +1] >> 3) & 0x02) ) - bias ;
		segdata[ 6* length + ix] = (float)(((vdifdata_in[4*ix +1] >> 3) & 0x01) | ((vdifdata_in[4*ix +1] >> 1) & 0x02) ) - bias ;
		segdata[ 7* length + ix] = (float)(((vdifdata_in[4*ix +1] >> 1) & 0x01) | ((vdifdata_in[4*ix +1] << 1) & 0x02) ) - bias ;
		segdata[ 8* length + ix] = (float)(((vdifdata_in[4*ix +2] >> 7) & 0x01) | ((vdifdata_in[4*ix +2] >> 5) & 0x02) ) - bias ;
		segdata[ 9* length + ix] = (float)(((vdifdata_in[4*ix +2] >> 5) & 0x01) | ((vdifdata_in[4*ix +2] >> 3) & 0x02) ) - bias ;
		segdata[10* length + ix] = (float)(((vdifdata_in[4*ix +2] >> 3) & 0x01) | ((vdifdata_in[4*ix +2] >> 1) & 0x02) ) - bias ;
		segdata[11* length + ix] = (float)(((vdifdata_in[4*ix +2] >> 1) & 0x01) | ((vdifdata_in[4*ix +2] << 1) & 0x02) ) - bias ;
		segdata[12* length + ix] = (float)(((vdifdata_in[4*ix +3] >> 7) & 0x01) | ((vdifdata_in[4*ix +3] >> 5) & 0x02) ) - bias ;
		segdata[13* length + ix] = (float)(((vdifdata_in[4*ix +3] >> 5) & 0x01) | ((vdifdata_in[4*ix +3] >> 3) & 0x02) ) - bias ;
		segdata[14* length + ix] = (float)(((vdifdata_in[4*ix +3] >> 3) & 0x01) | ((vdifdata_in[4*ix +3] >> 1) & 0x02) ) - bias ;
		segdata[15* length + ix] = (float)(((vdifdata_in[4*ix +3] >> 1) & 0x01) | ((vdifdata_in[4*ix +3] << 1) & 0x02) ) - bias ;
		// segdata[             ix] = weight[ vdifdata_in[4*ix   ] >> 6  & bitmask];		// Stream 0
		// segdata[    length + ix] = weight[(vdifdata_in[4*ix   ] >> 4) & bitmask];		// Stream 1
		// segdata[ 2* length + ix] = weight[(vdifdata_in[4*ix   ] >> 2) & bitmask];		// Stream 2
		// segdata[ 3* length + ix] = weight[(vdifdata_in[4*ix   ]     ) & bitmask];		// Stream 3
		// segdata[ 4* length + ix] = weight[(vdifdata_in[4*ix +1] >> 6) & bitmask];		// Stream 4
		// segdata[ 5* length + ix] = weight[(vdifdata_in[4*ix +1] >> 4) & bitmask];		// Stream 5
		// segdata[ 6* length + ix] = weight[(vdifdata_in[4*ix +1] >> 2) & bitmask];		// Stream 6
		// segdata[ 7* length + ix] = weight[(vdifdata_in[4*ix +1]     ) & bitmask];		// Stream 7
		// segdata[ 8* length + ix] = weight[(vdifdata_in[4*ix +2] >> 6) & bitmask];		// Stream 8
		// segdata[ 9* length + ix] = weight[(vdifdata_in[4*ix +2] >> 4) & bitmask];		// Stream 9
		// segdata[10* length + ix] = weight[(vdifdata_in[4*ix +2] >> 2) & bitmask];		// Stream 10
		// segdata[11* length + ix] = weight[(vdifdata_in[4*ix +2]     ) & bitmask];		// Stream 11
		// segdata[12* length + ix] = weight[(vdifdata_in[4*ix +3] >> 6) & bitmask];		// Stream 12
		// segdata[13* length + ix] = weight[(vdifdata_in[4*ix +3] >> 4) & bitmask];		// Stream 13
		// segdata[14* length + ix] = weight[(vdifdata_in[4*ix +3] >> 2) & bitmask];		// Stream 14
		// segdata[15* length + ix] = weight[(vdifdata_in[4*ix +3]     ) & bitmask];		// Stream 15
	}
}
