/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://developer.gimp.org/core/standards/pat/
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/gimp_pat/xx_gimp_pat.h"
#include "../xx_fifth_data.h"

static bool sm_gimp(Abstractformat *f,uint64_t at,uint64_t end,bool pattern,uint64_t *header,uint64_t *stop,xx_pd_struct *pd) {
    uint8_t h[28],name[4096]; uint32_t hs,w,height,channels,fixed=pattern ? 24:28; uint64_t pixels,bytes; size_t i,n;
    if(fd_stop(pd) || !fd_range(at,fixed,end) || !pm_read(f,(int64_t)at,h,fixed)) return false;
    hs=xx_data_get_u32(h, 4, 0, true); w=xx_data_get_u32(h+8, 4, 0, true); height=xx_data_get_u32(h+12, 4, 0, true); channels=xx_data_get_u32(h+16, 4, 0, true);
    if(xx_data_get_u32(h+4, 4, 0, true)!=(pattern ? 1U:2U) || xx_rt_memcmp(h+20,pattern ? "GPAT":"GIMP",4) || hs<=fixed || hs-fixed>4096 || !w || !height || w>32768 || height>32768 || channels<1 || channels>4 || (!pattern && channels!=1 && channels!=4)) return false;
    if(!fd_mul(w,height,&pixels) || pixels>67108864 || !fd_mul(pixels,channels,&bytes) || bytes>268435456 || !fd_range(at,hs+bytes,end)) return false;
    n=hs-fixed; if(!pm_read(f,(int64_t)at+fixed,name,n) || name[n-1] || !fourth_utf8(name,n-1,pd)) return false;
    for(i=0;i<n-1;++i) if(!name[i]) return false;
    *header=at+hs; *stop=*header+bytes; return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint64_t header,stop; if(!sm_gimp(f,0,(uint64_t)pm_available(f),true,&header,&stop,pd)) return false;
    if(!pm_add(f,s,"descriptor.bin",0,(int64_t)header) || !pm_add(f,s,"pixels.bin",(int64_t)header,(int64_t)(stop-header))) return false;
    s->size=(int64_t)stop; return true;
}

void xx_gimp_pat_init(xx_gimp_pat *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GIMP_PAT,"pat"); } }
xx_gimp_pat *xx_gimp_pat_create(xx_io_device *d,int64_t b) { xx_gimp_pat *r=(xx_gimp_pat *)xx_mem_alloc(sizeof(*r)); if(r) xx_gimp_pat_init(r,d,b); return r; }
void xx_gimp_pat_destroy(xx_gimp_pat *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_gimp_pat_free(xx_gimp_pat *r) { if(r) { xx_gimp_pat_destroy(r); xx_mem_free(r); } }
bool xx_gimp_pat_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_gimp_pat_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
