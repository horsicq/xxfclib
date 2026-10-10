/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/ult_load.c, https://raw.githubusercontent.com/OpenMPT/openmpt/master/soundlib/Load_ult.cpp
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/tracker_ult/xx_tracker_ult.h"
#include "../common/xx_binary_cursor.h"

static bool sm_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t *measured) {
    if(s->count>=4096 || !binary_range(at,n,(uint64_t)pm_available(f)) || at+n>268435456 || !pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;
    if(at+n>*measured) { *measured=at+n; } return true;
}
static XXFC_MAYBE_UNUSED bool sm_zero(const uint8_t *p,size_t n) { size_t i; for(i=0;i<n;++i) if(p[i]) return false; return true; }
static bool sm_loop(uint32_t begin,uint32_t end,uint32_t length,bool enabled) { return !enabled || (begin<end && end<=length); }

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[48],b[66],orders[256]; uint32_t samples,channels,patterns,length[64],i,j,count=0; uint64_t measured=0,start; char label[48]; binary_cursor c={f,0,(uint64_t)pm_available(f),pd,0};
    if(!binary_get(&c,h,48) || xx_rt_memcmp(h,"MAS_UTrack_V004",15) || !binary_skip(&c,(uint64_t)h[47]*32) || !binary_get(&c,b,1) || (samples=b[0])>64) return false;
    if(!sm_emit(f,s,"ult-descriptor.bin",0,c.at,&measured)) return false;
    for(i=0;i<samples;++i) { uint32_t a,z,first,last,flags; start=c.at; if(!binary_get(&c,b,66)) return false;
        a=xx_data_get_u32(b+44, 4, 0, false); z=xx_data_get_u32(b+48, 4, 0, false); first=xx_data_get_u32(b+52, 4, 0, false); last=xx_data_get_u32(b+56, 4, 0, false); flags=b[61];
        if(first>last || last>16777216 || last-first>262144 || (flags!=0 && flags!=4 && flags!=8 && flags!=12 && flags!=20 && flags!=24 && flags!=28) || !xx_data_get_u16(b+62, 2, 0, false)) return false;
        length[i]=last-first; if(!sm_loop(a,z,length[i],!!(flags&8))) return false; if(flags&4) length[i]*=2;
        xx_rt_snprintf(label,sizeof(label),"sample-%u-descriptor.bin",i+1); if(!sm_emit(f,s,label,start,66,&measured)) return false;
    }
    start=c.at; if(!binary_get(&c,orders,256) || !binary_get(&c,b,2)) return false; channels=(uint32_t)b[0]+1; patterns=(uint32_t)b[1]+1; if(channels>32 || patterns>128) return false;
    for(i=0;i<256;++i) { if(orders[i]==255) break; if(orders[i]>=patterns) return false; ++count; } if(!count) return false;
    for(i=0;i<channels;++i) if(!binary_get(&c,b,1) || b[0]>15) return false;
    if(!sm_emit(f,s,"orders-panning.bin",start,c.at-start,&measured)) return false;
    for(i=0;i<channels;++i) { uint32_t events=0; start=c.at; while(events<patterns*64) { uint32_t repeats=1,note; if(!binary_get(&c,b,1)) return false; note=b[0];
            if(note==252) { if(!binary_get(&c,b,2) || !b[0]) return false; repeats=b[0]; note=b[1]; }
            if(note>60 || repeats>patterns*64-events || !binary_get(&c,b,4) || b[0]>samples) { return false; } events+=repeats;
        } xx_rt_snprintf(label,sizeof(label),"channel-%u-events.bin",i); if(!sm_emit(f,s,label,start,c.at-start,&measured)) return false;
    }
    for(j=0;j<samples;++j) if(length[j]) { start=c.at; if(!binary_skip(&c,length[j])) return false; xx_rt_snprintf(label,sizeof(label),"sample-%u.bin",j+1); if(!sm_emit(f,s,label,start,length[j],&measured)) return false; }
    s->size=(int64_t)measured; return true;
}

void xx_tracker_ult_init(xx_tracker_ult *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_ULT,"ult"); } }
xx_tracker_ult *xx_tracker_ult_create(xx_io_device *d,int64_t b) { xx_tracker_ult *r=(xx_tracker_ult *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_ult_init(r,d,b); return r; }
void xx_tracker_ult_destroy(xx_tracker_ult *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_ult_free(xx_tracker_ult *r) { if(r) { xx_tracker_ult_destroy(r); xx_mem_free(r); } }
bool xx_tracker_ult_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_ult_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
