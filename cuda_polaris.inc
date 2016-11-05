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
//-------- Format segment data with 1-bit quantization
__global__ void segform_1bit(
	unsigned char   *vdifdata_in,		// Input VDIF Data, 1-byte packed (for 8 samples)
	float   *segdata,					// Output Segment Data
	int		length)						// Length to copy
{
	float	bias = 0.5;
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < length) ){
		segdata[8* ix    ] = (float)((vdifdata_in[ix] >> 7) & 0x01) - bias ;
		segdata[8* ix + 1] = (float)((vdifdata_in[ix] >> 6) & 0x01) - bias ;
		segdata[8* ix + 2] = (float)((vdifdata_in[ix] >> 5) & 0x01) - bias ;
		segdata[8* ix + 3] = (float)((vdifdata_in[ix] >> 4) & 0x01) - bias ;
		segdata[8* ix + 4] = (float)((vdifdata_in[ix] >> 3) & 0x01) - bias ;
		segdata[8* ix + 5] = (float)((vdifdata_in[ix] >> 2) & 0x01) - bias ;
		segdata[8* ix + 6] = (float)((vdifdata_in[ix] >> 1) & 0x01) - bias ;
		segdata[8* ix + 7] = (float)((vdifdata_in[ix]     ) & 0x01) - bias ;
	}
}
//-------- Format segment data with 2-bit quantization
__global__ void segform_2bit(
	unsigned char   *vdifdata_in,		// Input VDIF Data, 1-byte packed (for 4 samples)
	float   *segdata,					// Output Segment Data
	int		length)						// Length to copy
{
	int	hex_length = length >> 2;
	float	bias = 1.5;
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < hex_length) ){
		segdata[4* ix     ] = (float)(((vdifdata_in[ix] >> 7) & 0x01) | ((vdifdata_in[ix] >> 5) & 0x02) ) - bias ;
		segdata[4* ix +  1] = (float)(((vdifdata_in[ix] >> 5) & 0x01) | ((vdifdata_in[ix] >> 3) & 0x02) ) - bias ;
		segdata[4* ix +  2] = (float)(((vdifdata_in[ix] >> 3) & 0x01) | ((vdifdata_in[ix] >> 1) & 0x02) ) - bias ;
		segdata[4* ix +  3] = (float)(((vdifdata_in[ix] >> 1) & 0x01) | ((vdifdata_in[ix] << 1) & 0x02) ) - bias ;
	}
}
//-------- Format segment data with 4-bit quantization
__global__ void segform_4bit(
	unsigned char   *vdifdata_in,		// Input VDIF Data, 1-byte packed (for 2 samples)
	float	*segdata,			// Output Segment Data
	int		length)				// Length to copy
{
	float	bias = 7.5;			// Mean value
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	
	if( (ix >= 0) && (ix < length) ){
		segdata[2* ix     ] = (float)(((vdifdata_in[ix] >> 7) & 0x01) | ((vdifdata_in[ix] >> 5) & 0x02) | ((vdifdata_in[ix] >> 3) & 0x04) | ((vdifdata_in[ix] >> 1) & 0x08)) - bias ;
		segdata[2* ix +  1] = (float)(((vdifdata_in[ix] >> 1) & 0x01) | ((vdifdata_in[ix] >> 1) & 0x02) | ((vdifdata_in[ix] << 1) & 0x04) | ((vdifdata_in[ix] << 3) & 0x08)) - bias ;
	}
}
//-------- Format segment data with 8-bit quantization
__global__ void segform_8bit(
	unsigned char   *vdifdata_in,		// Input VDIF Data, 1-byte packed (for 2 samples)
	float	*segdata,			// Output Segment Data
	int		length)				// Length to copy
{
	int ix = blockIdx.x* blockDim.x + threadIdx.x;
	if( (ix >= 0) && (ix < length) ){
		segdata[ix] = (float)(((vdifdata_in[ix] >> 7) & 0x01) | ((vdifdata_in[ix] >> 5) & 0x02) | ((vdifdata_in[ix] >> 3) & 0x04) | ((vdifdata_in[ix] >> 1) & 0x08) | ((vdifdata_in[ix] << 1) & 0x10) | ((vdifdata_in[ix] << 3) & 0x20) | ((vdifdata_in[ix] << 5) & 0x40) | ((vdifdata_in[ix] << 7) & 0x80)) - 127.5;
	}
}
