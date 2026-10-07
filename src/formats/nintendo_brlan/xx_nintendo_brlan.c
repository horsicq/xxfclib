/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/magcius/noclip.website/main/src/Common/NW4R/lyt/Layout.ts
 * Big-endian RLAN version8, one pai1 block with1-32 pane bindings, one RLPA Hermite group each and1-10 tracks. Checks disjoint metadata/key tables, finite ordered keyframes and channel IDs. Exports encoded pai1 component; texture/material/visibility animation, other curve types and playback unsupported.
 */
#include "xxfclib/formats/nintendo_brlan/xx_nintendo_brlan.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static uint32_t g32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool finite32(const uint8_t *p,bool be) { return (g32(p,be)&0x7f800000U)!=0x7f800000U; }
typedef struct rg { uint64_t at,n; } rg;
static bool reserve(rg *r,unsigned *count,unsigned maximum,uint64_t at,uint64_t n,uint64_t lower,uint64_t end) { unsigned i; if(*count>=maximum || at<lower || !span(at,n,end)) return false; for(i=0;i<*count;++i) if(overlap(at,n,r[i].at,r[i].n)) return false; r[*count].at=at; r[*count].n=n; ++*count; return true; }
static bool nw_header(Abstractformat *f,const char *magic,uint16_t version,uint32_t *total,uint16_t *count,xx_pd_struct *pd) { uint8_t h[16]; if(stop(pd) || !pm_read(f,0,h,16) || xx_rt_memcmp(h,magic,4) || xx_data_get_u16(h+4, 2, 0, true)!=0xfeff || xx_data_get_u16(h+6, 2, 0, true)!=version || xx_data_get_u16(h+12, 2, 0, true)!=16 || !(*count=xx_data_get_u16(h+14, 2, 0, true)) || *count>1024 || (*total=xx_data_get_u32(h+8, 4, 0, true))<16 || *total>(uint64_t)pm_available(f)) return false; return true; }
static bool section(Abstractformat *f,uint64_t at,uint64_t total,const char *magic,uint32_t n,xx_pd_struct *pd) { uint8_t h[8]; return !stop(pd) && n>=8 && span(at,n,total) && pm_read(f,(int64_t)at,h,8) && !xx_rt_memcmp(h,magic,4) && xx_data_get_u32(h+4, 4, 0, true)==n; }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint32_t total,n,bindings,table,i,j,nranges=0; uint16_t blocks; uint8_t h[28],p[12],q[4]; uint64_t end; rg ranges[1024];
    if(!nw_header(f,"RLAN",8,&total,&blocks,pd) || blocks!=1 || !pm_read(f,16,h,20) || !section(f,16,total,"pai1",n=xx_data_get_u32(h+4, 4, 0, true),pd) || n<20 || n+16U!=(uint64_t)total || !xx_data_get_u16(h+8, 2, 0, true) || h[10]>1 || h[11] || xx_data_get_u16(h+12, 2, 0, true) || !(bindings=xx_data_get_u16(h+14, 2, 0, true)) || bindings>32) { return false; } end=n; table=xx_data_get_u32(h+16, 4, 0, true);
    if(!reserve(ranges,&nranges,1024,table,(uint64_t)bindings*4,20,end)) return false;
    for(i=0;i<bindings;++i) { uint32_t binding,tracks,k; uint64_t group; if(stop(pd) || !pm_read(f,16+(int64_t)table+i*4,q,4) || !reserve(ranges,&nranges,1024,binding=xx_data_get_u32(q, 4, 0, true),28,20,end) || !pm_read(f,16+(int64_t)binding,h,28) || !xx_rt_memchr(h,0,20) || h[20]!=1 || h[21] || h[22] || h[23]) return false;
      group=(uint64_t)binding+xx_data_get_u32(h+24, 4, 0, true); if(!span(group,8,end) || !pm_read(f,16+(int64_t)group,h,8) || xx_rt_memcmp(h,"RLPA",4) || !(tracks=h[4]) || tracks>10 || h[5] || h[6] || h[7] || !reserve(ranges,&nranges,1024,group,8+(uint64_t)tracks*4,20,end)) return false;
      for(j=0;j<tracks;++j) { uint32_t keys; uint64_t track,keyat; uint32_t previous=0; if(!pm_read(f,16+(int64_t)group+8+j*4,q,4) || !reserve(ranges,&nranges,1024,track=group+xx_data_get_u32(q, 4, 0, true),12,20,end) || !pm_read(f,16+(int64_t)track,p,12) || p[0] || p[1]>9 || p[2]!=2 || p[3] || !(keys=xx_data_get_u16(p+4, 2, 0, true)) || keys>4096 || xx_data_get_u16(p+6, 2, 0, true)) return false;
        keyat=track+xx_data_get_u32(p+8, 4, 0, true); if(!reserve(ranges,&nranges,1024,keyat,(uint64_t)keys*12,20,end)) return false;
        for(k=0;k<keys;++k) { uint32_t time; if(stop(pd) || !pm_read(f,16+(int64_t)keyat+k*12,p,12) || !finite32(p,true) || !finite32(p+4,true) || !finite32(p+8,true) || ((time=xx_data_get_u32(p, 4, 0, true))&0x80000000U) || (k && time<=previous)) return false; previous=time; }
      }
    }
    if(!emit(f,s,"pane-animation.bin",16,n,total)) { return false; } s->size=total; return true;

}

void xx_nintendo_brlan_init(xx_nintendo_brlan *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BRLAN,"brlan"); } }
xx_nintendo_brlan *xx_nintendo_brlan_create(xx_io_device *d,int64_t b) { xx_nintendo_brlan *r=(xx_nintendo_brlan *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_brlan_init(r,d,b); return r; }
void xx_nintendo_brlan_destroy(xx_nintendo_brlan *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_brlan_free(xx_nintendo_brlan *r) { if(r) { xx_nintendo_brlan_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_brlan_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_brlan_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
