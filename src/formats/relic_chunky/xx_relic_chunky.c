/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ModernMAK/Relic-Game-Tool/main/src/relic/chunky/chunky/header.py
 * Relic Chunky3.1 headers and bounded FOLD/DATA hierarchy, up to4096 chunks/depth32 with28-byte chunk headers. Exports named encoded DATA components; other revisions, typed asset decoding, rendering and game execution unsupported.
 */
#include "xxfclib/formats/relic_chunky/xx_relic_chunky.h"
#include "../xx_payload_members.h"

static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool take(Abstractformat *f,uint64_t *at,uint64_t end,void *p,size_t n,xx_pd_struct *pd) { if(stop(pd) || !span(*at,n,end) || !pm_read(f,(int64_t)*at,p,n)) return false; *at+=n; return true; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool chunky_nodes(Abstractformat *f,pm_stream *s,uint64_t at,uint64_t end,uint64_t total,unsigned depth,unsigned *count,xx_pd_struct *pd) {
    uint8_t h[28],name[4096]; uint32_t n,size,i; uint64_t next; char label[40];
    if(depth>32) return false; while(at<end) {
      if(++*count>4096 || !take(f,&at,end,h,28,pd) || (xx_rt_memcmp(h,"FOLD",4) && xx_rt_memcmp(h,"DATA",4)) || !pm_le32(h+8) || pm_le32(h+8)>65535) return false;
      for(i=4;i<8;++i) if(h[i]<32 || h[i]>126) return false; size=pm_le32(h+12); n=pm_le32(h+16);
      if(n>4096 || !take(f,&at,end,name,n,pd) || !span(at,size,end)) return false; for(i=0;i<n;++i) if(name[i]>127 || (name[i]<32 && name[i])) return false; next=at+size;
      if(!xx_rt_memcmp(h,"FOLD",4)) { if(!chunky_nodes(f,s,at,next,total,depth+1,count,pd)) return false; }
      else { xx_rt_snprintf(label,sizeof(label),"chunk-%c%c%c%c.bin",h[4],h[5],h[6],h[7]); if(!emit(f,s,label,at,size,total)) return false; } at=next;
    } return at==end;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[36]; uint64_t total=(uint64_t)pm_available(f); unsigned count=0;
    if(!pm_read(f,0,h,36) || xx_rt_memcmp(h,"Relic Chunky\r\n\x1a\0",16) || pm_le32(h+16)!=3 || pm_le32(h+20)!=1 || pm_le32(h+24)!=36 || pm_le32(h+28)!=28 || pm_le32(h+32)!=1 || !chunky_nodes(f,s,36,total,total,0,&count,pd) || !s->count) return false;
    s->size=(int64_t)total; return true;

}

void xx_relic_chunky_init(xx_relic_chunky *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_RELIC_CHUNKY,"chunky"); } }
xx_relic_chunky *xx_relic_chunky_create(xx_io_device *d,int64_t b) { xx_relic_chunky *r=(xx_relic_chunky *)xx_mem_alloc(sizeof(*r)); if(r) xx_relic_chunky_init(r,d,b); return r; }
void xx_relic_chunky_destroy(xx_relic_chunky *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_relic_chunky_free(xx_relic_chunky *r) { if(r) { xx_relic_chunky_destroy(r); xx_mem_free(r); } }
bool xx_relic_chunky_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_relic_chunky_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
