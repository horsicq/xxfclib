/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/mtm_load.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/tracker_mtm/xx_tracker_mtm.h"
#include "../xx_fifth_data.h"

static bool sm_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t *measured) {
    if(s->count>=4096 || !fd_range(at,n,(uint64_t)pm_available(f)) || at+n>268435456 || !pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;
    if(at+n>*measured) { *measured=at+n; } return true;
}
static XXFC_MAYBE_UNUSED bool sm_zero(const uint8_t *p,size_t n) { size_t i; for(i=0;i<n;++i) if(p[i]) return false; return true; }
static XXFC_MAYBE_UNUSED bool sm_loop(uint32_t begin,uint32_t end,uint32_t length,bool enabled) { return !enabled || (begin<end && end<=length); }

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[66],b[192],orders[128]; uint32_t tracks,patterns,samples,channels,count,i,j,lengths[63]; uint64_t measured=0,start; fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0}; char label[48];
    if(!fd_get(&c,h,66) || xx_rt_memcmp(h,"MTM\x10",4)) { return false; } tracks=xx_data_get_u16(h+24, 2, 0, false); patterns=(uint32_t)h[26]+1; count=(uint32_t)h[27]+1; samples=h[30]; channels=h[33];
    if(!tracks || tracks>1024 || patterns>128 || count>128 || samples>63 || h[31] || h[32]!=64 || !channels || channels>32) { return false; } for(i=0;i<32;++i) if(h[34+i]>15) return false;
    for(i=0;i<samples;++i) { uint32_t len,a,z; if(!fd_get(&c,b,37)) return false; len=xx_data_get_u32(b+22, 4, 0, false); a=xx_data_get_u32(b+26, 4, 0, false); z=xx_data_get_u32(b+30, 4, 0, false);
        if(len>16777216 || b[34]>15 || b[35]>64 || b[36]>1 || a>len || z>len || (z && a>=z) || ((b[36]&1) && ((len|a|z)&1))) { return false; } lengths[i]=len; }
    if(!fd_get(&c,orders,128)) { return false; } for(i=0;i<count;++i) if(orders[i]>=patterns) return false;
    if(!sm_emit(f,s,"mtm-descriptor.bin",0,c.at,&measured)) return false;
    for(i=0;i<tracks;++i) { start=c.at; if(!fd_get(&c,b,192)) return false;
        for(j=0;j<64;++j) { uint8_t *p=b+j*3; if(((p[0]&3U)<<4)+(p[1]>>4)>samples) return false; }
        xx_rt_snprintf(label,sizeof(label),"track-%u.bin",i+1); if(!sm_emit(f,s,label,start,192,&measured)) return false;
    }
    start=c.at; for(i=0;i<patterns;++i) { if(!fd_get(&c,b,64)) return false; for(j=0;j<32;++j) if(xx_data_get_u16(b+j*2, 2, 0, false)>tracks) return false; }
    if(!sm_emit(f,s,"pattern-tracks.bin",start,c.at-start,&measured)) return false;
    if(xx_data_get_u16(h+28, 2, 0, false)) { start=c.at; if(!fd_skip(&c,xx_data_get_u16(h+28, 2, 0, false)) || !sm_emit(f,s,"comment.txt",start,c.at-start,&measured)) return false; }
    for(i=0;i<samples;++i) if(lengths[i]) { start=c.at; if(!fd_skip(&c,lengths[i])) return false; xx_rt_snprintf(label,sizeof(label),"sample-%u.bin",i+1); if(!sm_emit(f,s,label,start,lengths[i],&measured)) return false; }
    s->size=(int64_t)measured; return true;
}

void xx_tracker_mtm_init(xx_tracker_mtm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_MTM,"mtm"); } }
xx_tracker_mtm *xx_tracker_mtm_create(xx_io_device *d,int64_t b) { xx_tracker_mtm *r=(xx_tracker_mtm *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_mtm_init(r,d,b); return r; }
void xx_tracker_mtm_destroy(xx_tracker_mtm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_mtm_free(xx_tracker_mtm *r) { if(r) { xx_tracker_mtm_destroy(r); xx_mem_free(r); } }
bool xx_tracker_mtm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_mtm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
