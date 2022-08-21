//	VDIFutc.c : extract UTC in the VDIF header
//
//	Author : Seiji Kameno
//	Created: 2014/08/29
//	Modified: 2016/11/3 for ADS3000+
//
#include "shm_VDIF.inc"
//-------- UTC in the VDIF header
int	VDIFutc(
	unsigned char		*vdifhead_ptr,	// IN: VDIF header (32 bytes)
	struct SHM_PARAM	*param_ptr)		// OUT: UTC will be set in param_ptr
{
	int	ref_sec = 0;	// Seconds from reference date
	int	ref_epoch = 0;	// Half-year periods from Y2000
    int sod;            // Second of the day

	ref_sec    = ((vdifhead_ptr[0] & 0x3f) << 24) + (vdifhead_ptr[1] << 16) + (vdifhead_ptr[2] << 8) + vdifhead_ptr[3];
	ref_epoch  = (vdifhead_ptr[4]      ) & 0x3f;
	param_ptr->year = 2000 + ref_epoch/2;
	param_ptr->doy  =  ref_sec / 86400 + (ref_epoch%2)* 182;
	if(param_ptr->year % 4 == 0){	param_ptr->doy++;}
    sod = ref_sec%86400;            // second of the day
    param_ptr->hour = sod / 3600;
    param_ptr->min = (sod % 3600) / 60;
    param_ptr->sec = (sod % 60);
	return(ref_sec);
}
