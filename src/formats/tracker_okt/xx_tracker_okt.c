/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/okt_load.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/tracker_okt/xx_tracker_okt.h"
#include "../xx_fifth_data.h"

static bool sm_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t *measured) {
    if(s->count>=4096 || !fd_range(at,n,(uint64_t)pm_available(f)) || at+n>268435456 || !pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;
    if(at+n>*measured) { *measured=at+n; } return true;
}
static XXFC_MAYBE_UNUSED bool sm_zero(const uint8_t *p,size_t n) { size_t i; for(i=0;i<n;++i) if(p[i]) return false; return true; }
static XXFC_MAYBE_UNUSED bool sm_loop(uint32_t begin,uint32_t end,uint32_t length,bool enabled) { return !enabled || (begin<end && end<=length); }

static bool sm_okt_chunk(fd_cursor *c,const char *tag,uint32_t *length,uint64_t *begin) {
    uint8_t h[8]; *begin=c->at; if(!fd_get(c,h,8) || xx_rt_memcmp(h,tag,4)) return false; *length=xx_data_get_u32(h+4, 4, 0, true); return fd_range(c->at,*length,c->end);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t magic[8],b[1152],orders[128]; uint32_t n,channels=4,patterns,count,ns=0,lens[36],i,j; uint64_t measured=0,start; char label[48]; fd_cursor c={f,0,(uint64_t)pm_available(f),pd,0};
    if(!fd_get(&c,magic,8) || xx_rt_memcmp(magic,"OKTASONG",8) || !sm_emit(f,s,"okt-magic.bin",0,8,&measured)) return false;
    if(!sm_okt_chunk(&c,"CMOD",&n,&start) || n!=8 || !fd_get(&c,b,8)) { return false; } for(i=0;i<4;++i) { if(xx_data_get_u16(b+i*2, 2, 0, true)>1) return false; channels+=xx_data_get_u16(b+i*2, 2, 0, true); }
    if(!sm_emit(f,s,"cmod.bin",start,c.at-start,&measured) || !sm_okt_chunk(&c,"SAMP",&n,&start) || n!=1152 || !fd_get(&c,b,1152)) return false;
    for(i=0;i<36;++i) { uint8_t *p=b+i*32; uint32_t len=xx_data_get_u32(p+20, 4, 0, true),a=(uint32_t)xx_data_get_u16(p+24, 2, 0, true)*2,z=(uint32_t)xx_data_get_u16(p+26, 2, 0, true)*2;
        if(len>16777216 || (len&1) || xx_data_get_u16(p+28, 2, 0, true)>64 || xx_data_get_u16(p+30, 2, 0, true)>2 || (z>2 && (a>len || z>len-a))) { return false; } if(len) lens[ns++]=len; }
    if(!sm_emit(f,s,"samp.bin",start,c.at-start,&measured) || !sm_okt_chunk(&c,"SPEE",&n,&start) || n!=2 || !fd_get(&c,b,2) || !xx_data_get_u16(b, 2, 0, true) || xx_data_get_u16(b, 2, 0, true)>255 || !sm_emit(f,s,"spee.bin",start,c.at-start,&measured)) return false;
    if(!sm_okt_chunk(&c,"SLEN",&n,&start) || n!=2 || !fd_get(&c,b,2) || !(patterns=xx_data_get_u16(b, 2, 0, true)) || patterns>128 || !sm_emit(f,s,"slen.bin",start,c.at-start,&measured)) return false;
    if(!sm_okt_chunk(&c,"PLEN",&n,&start) || n!=2 || !fd_get(&c,b,2) || !(count=xx_data_get_u16(b, 2, 0, true)) || count>128 || !sm_emit(f,s,"plen.bin",start,c.at-start,&measured)) return false;
    if(!sm_okt_chunk(&c,"PATT",&n,&start) || n<count || n>256 || !fd_get(&c,orders,count)) { return false; } for(i=0;i<count;++i) if(orders[i]>=patterns) return false;
    if(!fd_skip(&c,n-count) || !sm_emit(f,s,"patt.bin",start,c.at-start,&measured)) return false;
    for(i=0;i<patterns;++i) { uint32_t rows; if(!sm_okt_chunk(&c,"PBOD",&n,&start) || n<2 || !fd_get(&c,b,2) || !(rows=xx_data_get_u16(b, 2, 0, true)) || rows>256 || n!=2+rows*channels*4) return false;
        for(j=0;j<rows*channels;++j) if(!fd_get(&c,b,4) || b[0]>36 || b[1]>=36 || b[2]>31) return false;
        xx_rt_snprintf(label,sizeof(label),"pattern-%u-pbod.bin",i); if(!sm_emit(f,s,label,start,c.at-start,&measured)) return false;
    }
    for(i=0;i<ns;++i) { if(!sm_okt_chunk(&c,"SBOD",&n,&start) || n!=lens[i] || !fd_skip(&c,n)) return false; xx_rt_snprintf(label,sizeof(label),"sample-%u-sbod.bin",i+1); if(!sm_emit(f,s,label,start,c.at-start,&measured)) return false; }
    s->size=(int64_t)measured; return true;
}

void xx_tracker_okt_init(xx_tracker_okt *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_OKT,"okt"); } }
xx_tracker_okt *xx_tracker_okt_create(xx_io_device *d,int64_t b) { xx_tracker_okt *r=(xx_tracker_okt *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_okt_init(r,d,b); return r; }
void xx_tracker_okt_destroy(xx_tracker_okt *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_okt_free(xx_tracker_okt *r) { if(r) { xx_tracker_okt_destroy(r); xx_mem_free(r); } }
bool xx_tracker_okt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_okt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
