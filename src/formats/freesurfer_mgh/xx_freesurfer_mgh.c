/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/freesurfer/freesurfer/dev/matlab/load_mgh.m */
#include "xxfclib/formats/freesurfer_mgh/xx_freesurfer_mgh.h"
#include "../xx_sixth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[284];uint64_t n=1;unsigned i,width,t;int64_t available=pm_available(f);
    if(fd_stop(pd) || available<284 || !pm_read(f,0,h,sizeof(h)) || pm_be32(h)!=1 || pm_be16(h+28)>1) return false;
    t=pm_be32(h+20);switch(t) {case 0:width=1;break;case 1:case 3:width=4;break;case 4:case 10:width=2;break;default:return false;}
    for(i=0;i<4;++i) {uint32_t d=pm_be32(h+4+i*4);if(!d || d>INT32_MAX || !fd_mul(n,d,&n)) return false;}
    if(!fd_mul(n,width,&n) || !fd_range(284,n,(uint64_t)available) || !pm_add(f,s,"mgh-header.bin",0,284) || !pm_add(f,s,"voxels.bin",284,(int64_t)n)) return false;
    s->size=284+(int64_t)n;return true;
}

void xx_freesurfer_mgh_init(xx_freesurfer_mgh *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_FREESURFER_MGH,"freesurfer_mgh"); } }
xx_freesurfer_mgh *xx_freesurfer_mgh_create(xx_io_device *d,int64_t b) { xx_freesurfer_mgh *r=(xx_freesurfer_mgh *)xx_mem_alloc(sizeof(*r)); if(r) xx_freesurfer_mgh_init(r,d,b); return r; }
void xx_freesurfer_mgh_destroy(xx_freesurfer_mgh *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_freesurfer_mgh_free(xx_freesurfer_mgh *r) { if(r) { xx_freesurfer_mgh_destroy(r); xx_mem_free(r); } }
bool xx_freesurfer_mgh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_freesurfer_mgh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
