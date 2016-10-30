#include "shm_VDIF.inc"

int vdif_head_extract(
	unsigned char	*vdifhead_ptr,	// 32-byte VDIF header
	struct vdifhead *vdif_header)	// Header in Struct
{
	vdif_header->I			= (vdifhead_ptr[1] >> 7 );
	vdif_header->ref_sec 	= ((vdifhead_ptr[0] & 0x3f) << 24) + (vdifhead_ptr[1] << 16) + (vdifhead_ptr[2] << 8) + vdifhead_ptr[3];
	vdif_header->ref_epoch 	= (vdifhead_ptr[4]      ) & 0x3f;
	vdif_header->frameID 	= (vdifhead_ptr[5] << 16) + (vdifhead_ptr[6] << 8) + vdifhead_ptr[7];
	vdif_header->frameBytes = (vdifhead_ptr[9] << 16) + (vdifhead_ptr[10] << 8) + vdifhead_ptr[11];
	vdif_header->qbit 		= (vdifhead_ptr[12] >> 2) & 0x1f;
	vdif_header->thredID 	= (vdifhead_ptr[12] & 0x03) << 8 + vdifhead_ptr[13];
	vdif_header->effBit 	= (vdifhead_ptr[17]     ) & 0x1f;
	vdif_header->sampleDiv 	= (vdifhead_ptr[18] >> 4) & 0x0f;
	vdif_header->split		= (vdifhead_ptr[18]     ) & 0x0f;
	vdif_header->TV			= (vdifhead_ptr[19] >> 6) & 0x03;
	vdif_header->ref_epochS	= (vdifhead_ptr[19]     ) & 0x3f;
	vdif_header->ref_secS	= ((vdifhead_ptr[20] & 0x3f) << 24) + (vdifhead_ptr[21] << 16) + (vdifhead_ptr[22] << 8) + vdifhead_ptr[23];
	vdif_header->sec_count 	= (vdifhead_ptr[24]     ) & 0xff;
	return(0);
}
