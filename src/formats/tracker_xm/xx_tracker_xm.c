/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/xm_load.c, https://raw.githubusercontent.com/libxmp/libxmp/master/src/loaders/xm.h
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/tracker_xm/xx_tracker_xm.h"
#include "../common/xx_binary_cursor.h"

static bool sm_emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t at,uint64_t n,uint64_t *measured) {
    if(s->count>=4096 || !binary_range(at,n,(uint64_t)pm_available(f)) || at+n>268435456 || !pm_add(f,s,label,(int64_t)at,(int64_t)n)) return false;
    if(at+n>*measured) { *measured=at+n; } return true;
}
static XXFC_MAYBE_UNUSED bool sm_zero(const uint8_t *p,size_t n) { size_t i; for(i=0;i<n;++i) if(p[i]) return false; return true; }
static XXFC_MAYBE_UNUSED bool sm_loop(uint32_t begin,uint32_t end,uint32_t length,bool enabled) { return !enabled || (begin<end && end<=length); }

static bool sm_xm_pattern(Abstractformat *f,uint64_t at,uint32_t n,uint32_t rows,uint32_t channels,uint32_t instruments,xx_pd_struct *pd) {
    uint8_t b[5]; uint32_t i,j; binary_cursor c={f,at,at+n,pd,0};
    if(!n) return true;
    for(i=0;i<rows*channels;++i) { uint8_t event[5]={0}; if(!binary_get(&c,b,1)) return false;
        if(b[0]&128) { uint8_t mask=b[0]; if(mask&0x60) return false; for(j=0;j<5;++j) if(mask&(1U<<j)) { if(!binary_get(&c,b,1)) return false; event[j]=b[0]; } }
        else { event[0]=b[0]; if(!binary_get(&c,event+1,4)) return false; }
        if(event[0]>97 || event[1]>instruments || (event[2] && event[2]<0x10) || (event[2]>0x50 && event[2]<0x60) || event[3]>35) return false;
    } return c.at==c.end;
}
static bool sm_xm_envelope(const uint8_t *p,unsigned count,uint8_t sustain,uint8_t begin,uint8_t end,uint8_t flags) {
    unsigned i; uint16_t last=0; if(count>12 || flags>7 || ((flags&1) && !count) || ((flags&2) && sustain>=count) || ((flags&4) && (begin>end || end>=count))) return false;
    for(i=0;i<count;++i) { uint16_t x=xx_data_get_u16(p+i*4, 2, 0, false),y=xx_data_get_u16(p+i*4+2, 2, 0, false); if(y>64 || (i && x<=last)) return false; last=x; } return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[336]={0},b[263],sample[40]; uint32_t patterns,instruments,channels,orders,i,j,count,len,head,lengths[16],sample_total=0; uint64_t at,measured=0,start,end=(uint64_t)pm_available(f); char label[64];
    if(binary_stop(pd) || !pm_read(f,0,h,80) || xx_rt_memcmp(h,"Extended Module: ",17) || h[37]!=26 || xx_data_get_u16(h+58, 2, 0, false)!=0x104) return false;
    orders=xx_data_get_u16(h+64, 2, 0, false); channels=xx_data_get_u16(h+68, 2, 0, false); patterns=xx_data_get_u16(h+70, 2, 0, false); instruments=xx_data_get_u16(h+72, 2, 0, false);
    if(!orders || orders>256 || xx_data_get_u16(h+66, 2, 0, false)>=orders || !channels || channels>32 || !patterns || patterns>128 || instruments>128 || xx_data_get_u16(h+74, 2, 0, false)>1 || !xx_data_get_u16(h+76, 2, 0, false) || xx_data_get_u16(h+76, 2, 0, false)>31 || xx_data_get_u16(h+78, 2, 0, false)<32 || xx_data_get_u16(h+78, 2, 0, false)>255) return false;
    head=xx_data_get_u32(h+60, 4, 0, false); if(head<20+orders || head>276 || !pm_read(f,80,h+80,head-20)) return false; for(i=0;i<orders;++i) if(h[80+i]>=patterns) return false; at=60+head; if(!sm_emit(f,s,"xm-descriptor.bin",0,at,&measured)) return false;
    for(i=0;i<patterns;++i) { uint32_t rows,packed; if(binary_stop(pd) || !binary_range(at,9,end) || !pm_read(f,(int64_t)at,b,9)) return false; head=xx_data_get_u32(b, 4, 0, false); rows=xx_data_get_u16(b+5, 2, 0, false); packed=xx_data_get_u16(b+7, 2, 0, false);
        if(head!=9 || b[4] || !rows || rows>256 || !binary_range(at+9,packed,end) || !sm_xm_pattern(f,at+9,packed,rows,channels,instruments,pd)) return false;
        xx_rt_snprintf(label,sizeof(label),"pattern-%u.bin",i); if(!sm_emit(f,s,label,at,9+packed,&measured)) return false; at+=9+packed;
    }
    for(i=0;i<instruments;++i) { if(binary_stop(pd) || !binary_range(at,29,end) || !pm_read(f,(int64_t)at,b,29)) return false; start=at; head=xx_data_get_u32(b, 4, 0, false); count=xx_data_get_u16(b+27, 2, 0, false);
        if(b[26] || count>16 || (count ? head!=263 : (head!=29 && head!=263)) || !binary_range(at,head,end) || !pm_read(f,(int64_t)at,b,head)) return false;
        if(count) { if(xx_data_get_u32(b+29, 4, 0, false)!=40 || sample_total>2048-count) return false; sample_total+=count; for(j=0;j<96;++j) if(b[33+j]>=count) return false;
            if(!sm_xm_envelope(b+129,b[225],b[227],b[228],b[229],b[233]) || !sm_xm_envelope(b+177,b[226],b[230],b[231],b[232],b[234]) || b[235]>3 || b[237]>15 || b[238]>63) return false;
        }
        xx_rt_snprintf(label,sizeof(label),"instrument-%u.bin",i+1); if(!sm_emit(f,s,label,start,head,&measured)) return false; at+=head;
        for(j=0;j<count;++j) { uint32_t a,z; if(binary_stop(pd) || !binary_range(at,40,end) || !pm_read(f,(int64_t)at,sample,40)) return false; len=xx_data_get_u32(sample, 4, 0, false); a=xx_data_get_u32(sample+4, 4, 0, false); z=xx_data_get_u32(sample+8, 4, 0, false);
            if(len>16777216 || sample[12]>64 || (sample[14]&~0x13U) || (sample[14]&3)==3 || sample[17] || a>len || z>len-a || ((sample[14]&3) && !z) || ((sample[14]&16) && ((len|a|z)&1))) return false;
            lengths[j]=len; xx_rt_snprintf(label,sizeof(label),"instrument-%u-sample-%u-descriptor.bin",i+1,j+1); if(!sm_emit(f,s,label,at,40,&measured)) return false; at+=40;
        }
        for(j=0;j<count;++j) if(lengths[j]) { xx_rt_snprintf(label,sizeof(label),"instrument-%u-sample-%u-delta.bin",i+1,j+1); if(binary_stop(pd) || !sm_emit(f,s,label,at,lengths[j],&measured)) return false; at+=lengths[j]; }
    } s->size=(int64_t)measured; return true;
}

void xx_tracker_xm_init(xx_tracker_xm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_XM,"xm"); } }
xx_tracker_xm *xx_tracker_xm_create(xx_io_device *d,int64_t b) { xx_tracker_xm *r=(xx_tracker_xm *)xx_mem_alloc(sizeof(*r)); if(r) xx_tracker_xm_init(r,d,b); return r; }
void xx_tracker_xm_destroy(xx_tracker_xm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_xm_free(xx_tracker_xm *r) { if(r) { xx_tracker_xm_destroy(r); xx_mem_free(r); } }
bool xx_tracker_xm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_xm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
