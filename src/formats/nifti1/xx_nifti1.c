/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/NIFTI-Imaging/nifti_clib/master/niftilib/nifti1.h */
#include "xxfclib/formats/nifti1/xx_nifti1.h"
#include "../xx_sixth_data.h"

static unsigned voxel_bits(unsigned t) {
    switch(t) {case 2:case 256:return 8;case 4:case 512:return 16;case 8:case 16:case 768:case 2304:return 32;
    case 32:case 64:case 1024:case 1280:return 64;case 128:return 24;case 1536:case 1792:return 128;case 2048:return 256;default:return 0;}
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[352],b[8];bool be;unsigned dim,i,bits;uint64_t count=1,at,n,end;int64_t available=pm_available(f);
    if(fd_stop(pd) || available<352 || !pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h+344,"n+1\0",4)) return false;
    be=xx_data_get_u32(h, 4, 0, true)==348;if(!be && xx_data_get_u32(h, 4, 0, false)!=348) return false;
    dim=xx_data_get_u16(h+40, 2, 0, be);bits=voxel_bits(xx_data_get_u16(h+70, 2, 0, be));
    if(dim<1 || dim>7 || !bits || bits!=xx_data_get_u16(h+72, 2, 0, be) || !sd_float32_uint(xx_data_get_u32(h+108, 4, 0, be),&at) || at<352) return false;
    for(i=0;i<dim;++i) {uint16_t d=xx_data_get_u16(h+42+i*2, 2, 0, be);if(!d || d>32767 || !fd_mul(count,d,&count)) return false;}
    if(!fd_mul(count,bits/8,&n) || !fd_range(at,n,(uint64_t)available)) { return false; } end=at+n;
    if(h[348]>1 || h[349] || h[350] || h[351] || !pm_add(f,s,"nifti-header.bin",0,h[348]?352:(int64_t)at)) return false;
    if(h[348]) {uint64_t p=352;unsigned extensions=0;
        while(p<at) {uint32_t z,code;char label[64];if(fd_stop(pd) || ++extensions>4096 || !fd_range(p,8,at) || !pm_read(f,(int64_t)p,b,8)) return false;
            z=xx_data_get_u32(b, 4, 0, be);code=xx_data_get_u32(b+4, 4, 0, be);if(z<16 || z%16 || code>INT32_MAX || !fd_range(p,z,at)) return false;
            xx_rt_snprintf(label,sizeof(label),"extension-%u-code-%u.bin",extensions-1,code);if(!pm_add(f,s,label,(int64_t)p,z)) return false;p+=z;
        }
    }
    if(!pm_add(f,s,"voxels.bin",(int64_t)at,(int64_t)n)) { return false; } s->size=(int64_t)end;return true;
}

void xx_nifti1_init(xx_nifti1 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NIFTI1,"nifti1"); } }
xx_nifti1 *xx_nifti1_create(xx_io_device *d,int64_t b) { xx_nifti1 *r=(xx_nifti1 *)xx_mem_alloc(sizeof(*r)); if(r) xx_nifti1_init(r,d,b); return r; }
void xx_nifti1_destroy(xx_nifti1 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nifti1_free(xx_nifti1 *r) { if(r) { xx_nifti1_destroy(r); xx_mem_free(r); } }
bool xx_nifti1_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nifti1_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
