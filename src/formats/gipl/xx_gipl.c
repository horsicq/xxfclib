/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/InsightSoftwareConsortium/ITK/master/Modules/IO/GIPL/src/itkGiplImageIO.cxx */
#include "xxfclib/formats/gipl/xx_gipl.h"
#include "../xx_sixth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[256];bool be;uint32_t magic;uint64_t n=1;unsigned i,width,t;int64_t available=pm_available(f);
    if(fd_stop(pd) || available<256 || !pm_read(f,0,h,sizeof(h))) return false;
    magic=xx_data_get_u32(h+252, 4, 0, true);be=magic==0xefffe9b0U || magic==0x2ae389b8U;
    if(!be && xx_data_get_u32(h+252, 4, 0, false)!=0xefffe9b0U && xx_data_get_u32(h+252, 4, 0, false)!=0x2ae389b8U) return false;
    t=xx_data_get_u16(h+8, 2, 0, be);switch(t) {case 7:case 8:width=1;break;case 15:case 16:width=2;break;case 31:case 32:case 64:width=4;break;case 65:width=8;break;default:return false;}
    for(i=0;i<4;++i) {unsigned d=xx_data_get_u16(h+i*2, 2, 0, be);if(!d || !fd_mul(n,d,&n)) return false;}
    if(!fd_mul(n,width,&n) || !fd_range(256,n,(uint64_t)available) || !pm_add(f,s,"gipl-header.bin",0,256) || !pm_add(f,s,"voxels.bin",256,(int64_t)n)) return false;
    s->size=256+(int64_t)n;return true;
}

void xx_gipl_init(xx_gipl *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GIPL,"gipl"); } }
xx_gipl *xx_gipl_create(xx_io_device *d,int64_t b) { xx_gipl *r=(xx_gipl *)xx_mem_alloc(sizeof(*r)); if(r) xx_gipl_init(r,d,b); return r; }
void xx_gipl_destroy(xx_gipl *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_gipl_free(xx_gipl *r) { if(r) { xx_gipl_destroy(r); xx_mem_free(r); } }
bool xx_gipl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_gipl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
