/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/669_load.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/tracker_669/xx_tracker_669.h"
#include "../xx_fifth_data.h"

static bool sm_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t *measured) {
    if(s->count>=4096 || !fd_range(at,n,(uint64_t)pm_available(f)) || at+n>268435456 || !pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;
    if(at+n>*measured) { *measured=at+n; } return true;
}
static XXFC_MAYBE_UNUSED bool sm_zero(const uint8_t *p,size_t n) { size_t i; for(i=0;i<n;++i) if(p[i]) return false; return true; }
static XXFC_MAYBE_UNUSED bool sm_loop(uint32_t begin,uint32_t end,uint32_t length,bool enabled) { return !enabled || (begin<end && end<=length); }

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[497],b[1536]; uint32_t samples,patterns,orders=0,i,j,length[64]; uint64_t measured=0,start; char label[48]; fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0};
    if(!fd_get(&c,h,497) || (xx_rt_memcmp(h,"if",2) && xx_rt_memcmp(h,"JN",2)) || (samples=h[110])>64 || !(patterns=h[111]) || patterns>128) return false;
    for(i=0;i<128;++i) { if(h[113+i]==255) break; if(h[113+i]>=patterns) return false; ++orders; } if(!orders || h[112]>=orders || h[240]!=255) return false;
    for(i=0;i<patterns;++i) if(!h[241+i] || h[241+i]>15 || h[369+i]>63) return false;
    if(!sm_emit(f,s,"669-descriptor.bin",0,497,&measured)) return false;
    for(i=0;i<samples;++i) { uint32_t a,z; start=c.at; if(!fd_get(&c,b,25)) return false; length[i]=pm_le32(b+13); a=pm_le32(b+17); z=pm_le32(b+21);
        if(length[i]>16777216 || a>length[i] || (z<0xfffff && z && (a>=z || z>length[i]))) return false;
        xx_rt_snprintf(label,sizeof(label),"sample-%u-descriptor.bin",i+1); if(!sm_emit(f,s,label,start,25,&measured)) return false; }
    for(i=0;i<patterns;++i) { start=c.at; if(!fd_get(&c,b,1536)) return false;
        for(j=0;j<512;++j) { uint8_t *p=b+j*3; if(p[0]<254 && (1+(p[1]>>4)+((p[0]&3U)<<4)>samples || (p[0]>>2)>63)) return false; if(p[2]!=255 && (p[2]>>4)>5) return false; }
        xx_rt_snprintf(label,sizeof(label),"pattern-%u.bin",i); if(!sm_emit(f,s,label,start,1536,&measured)) return false;
    }
    for(i=0;i<samples;++i) if(length[i]) { start=c.at; if(!fd_skip(&c,length[i])) return false; xx_rt_snprintf(label,sizeof(label),"sample-%u.bin",i+1); if(!sm_emit(f,s,label,start,length[i],&measured)) return false; }
    s->size=(int64_t)measured; return true;
}

void xx_tracker_669_init(xx_tracker_669 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_669,"669"); } }
xx_tracker_669 *xx_tracker_669_create(xx_io_device *d,int64_t b) { xx_tracker_669 *r=(xx_tracker_669 *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_669_init(r,d,b); return r; }
void xx_tracker_669_destroy(xx_tracker_669 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_669_free(xx_tracker_669 *r) { if(r) { xx_tracker_669_destroy(r); xx_mem_free(r); } }
bool xx_tracker_669_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_669_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
