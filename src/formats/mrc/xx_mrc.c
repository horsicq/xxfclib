/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.ccpem.ac.uk/mrc-format/mrc2014/ */
#include "xxfclib/formats/mrc/xx_mrc.h"
#include "../xx_sixth_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[1024];bool be;uint64_t count=1,n,ext,at;unsigned i,width,mode,axes[3];int64_t available=pm_available(f);
    if(fd_stop(pd) || available<1024 || !pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h+208,"MAP ",4)) return false;
    be=h[212]==0x11 && h[213]==0x11;
    if(!be && !(h[212]==0x44 && (h[213]==0x44 || h[213]==0x41))) return false;
    mode=fd_u32(h+12,be);switch(mode) {case 0:width=1;break;case 1:case 6:case 12:width=2;break;case 2:case 3:width=4;break;case 4:width=8;break;default:return false;}
    for(i=0;i<3;++i) {uint32_t d=fd_u32(h+i*4,be);axes[i]=fd_u32(h+64+i*4,be);
        if(!d || d>INT32_MAX || axes[i]<1 || axes[i]>3 || !fd_mul(count,d,&count)) return false;}
    if(axes[0]==axes[1] || axes[0]==axes[2] || axes[1]==axes[2] || fd_u32(h+220,be)>10) return false;
    ext=fd_u32(h+92,be);if(ext>INT32_MAX || !fd_mul(count,width,&n)) return false;at=1024+ext;
    if(!fd_range(at,n,(uint64_t)available) || !pm_add(f,s,"mrc-header.bin",0,1024)) return false;
    if(ext && !pm_add(f,s,"extended-header.bin",1024,(int64_t)ext)) return false;
    if(!pm_add(f,s,"volume.bin",(int64_t)at,(int64_t)n)) return false;s->size=(int64_t)(at+n);return true;
}

void xx_mrc_init(xx_mrc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MRC,"mrc"); } }
xx_mrc *xx_mrc_create(xx_io_device *d,int64_t b) { xx_mrc *r=(xx_mrc *)xx_mem_alloc(sizeof(*r)); if(r) xx_mrc_init(r,d,b); return r; }
void xx_mrc_destroy(xx_mrc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_mrc_free(xx_mrc *r) { if(r) { xx_mrc_destroy(r); xx_mem_free(r); } }
bool xx_mrc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_mrc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
