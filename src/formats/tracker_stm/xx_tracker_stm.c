/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/stm_load.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/tracker_stm/xx_tracker_stm.h"
#include "../common/xx_binary_cursor.h"

static bool sm_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t *measured) {
    if(s->count>=4096 || !binary_range(at,n,(uint64_t)pm_available(f)) || at+n>268435456 || !pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;
    if(at+n>*measured) { *measured=at+n; } return true;
}
static XXFC_MAYBE_UNUSED bool sm_zero(const uint8_t *p,size_t n) { size_t i; for(i=0;i<n;++i) if(p[i]) return false; return true; }
static XXFC_MAYBE_UNUSED bool sm_loop(uint32_t begin,uint32_t end,uint32_t length,bool enabled) { return !enabled || (begin<end && end<=length); }

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[48],b[32],orders[128]; uint32_t patterns,i,j,n,ptr[31],length[31],count=0; uint64_t measured=0,start,pattern_end; char label[48]; binary_cursor c={f,0,(uint64_t)pm_available(f),pd,0};
    if(!binary_get(&c,h,48) || xx_rt_memcmp(h+20,"!Scream!",8) || h[28]!=26 || h[29]!=2 || h[30]!=2 || h[31]!=21 || !(patterns=h[33]) || patterns>64 || h[34]>64) return false;
    for(i=0;i<31;++i) { uint32_t a,z; if(!binary_get(&c,b,32)) return false; length[i]=xx_data_get_u16(b+16, 2, 0, false); ptr[i]=(uint32_t)xx_data_get_u16(b+14, 2, 0, false)*16; a=xx_data_get_u16(b+18, 2, 0, false); z=xx_data_get_u16(b+20, 2, 0, false);
        if(b[22]>64 || a>length[i] || (z!=65535 && (z>length[i] || (z && a>=z))) || (length[i] && !xx_data_get_u16(b+24, 2, 0, false))) return false; }
    if(!binary_get(&c,orders,128)) { return false; } for(i=0;i<128;++i) { if(orders[i]>=99) break; if(orders[i]>=patterns) return false; ++count; } if(!count) return false;
    if(!sm_emit(f,s,"stm-descriptor.bin",0,c.at,&measured)) return false;
    for(i=0;i<patterns;++i) { start=c.at;
        for(j=0;j<256;++j) { if(!binary_get(&c,b,1)) return false; n=b[0]; if(n==251 || n==252 || n==253) continue;
            if(n<254 && ((n&15)>11 || (n>>4)>7)) { return false; } if(!binary_get(&c,b,3) || (b[0]>>3)>31) return false; }
        xx_rt_snprintf(label,sizeof(label),"pattern-%u.bin",i); if(!sm_emit(f,s,label,start,c.at-start,&measured)) return false;
    } pattern_end=c.at;
    for(i=0;i<31;++i) if(length[i]) { if(binary_stop(pd) || ptr[i]<pattern_end) return false; xx_rt_snprintf(label,sizeof(label),"sample-%u.bin",i+1); if(!sm_emit(f,s,label,ptr[i],length[i],&measured)) return false; }
    if(!binary_disjoint(s,(uint64_t)f->base_address)) { return false; } s->size=(int64_t)measured; return true;
}

void xx_tracker_stm_init(xx_tracker_stm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_STM,"stm"); } }
xx_tracker_stm *xx_tracker_stm_create(xx_io_device *d,int64_t b) { xx_tracker_stm *r=(xx_tracker_stm *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_stm_init(r,d,b); return r; }
void xx_tracker_stm_destroy(xx_tracker_stm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_stm_free(xx_tracker_stm *r) { if(r) { xx_tracker_stm_destroy(r); xx_mem_free(r); } }
bool xx_tracker_stm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_stm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
