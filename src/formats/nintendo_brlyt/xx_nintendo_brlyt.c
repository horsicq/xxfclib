/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/magcius/noclip.website/main/src/Common/NW4R/lyt/Layout.ts
 * Big-endian RLYT version8 with lyt1, basic pan1 pane hierarchies, texture/font lists and groups. Checks section framing, finite pane geometry, bounded strings and balanced hierarchy markers. Exports individual encoded sections; picture/text/window/material extensions, external resources and rendering unsupported.
 */
#include "xxfclib/formats/nintendo_brlyt/xx_nintendo_brlyt.h"
#include "../xx_payload_members.h"

static uint32_t g32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool take(Abstractformat *f,uint64_t *at,uint64_t end,void *p,size_t n,xx_pd_struct *pd) { if(stop(pd) || !span(*at,n,end) || !pm_read(f,(int64_t)*at,p,n)) return false; *at+=n; return true; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool finite32(const uint8_t *p,bool be) { return (g32(p,be)&0x7f800000U)!=0x7f800000U; }
static bool cstring(Abstractformat *f,uint64_t *at,uint64_t end,unsigned maximum,bool empty,xx_pd_struct *pd) { uint8_t c; unsigned i; for(i=0;i<maximum;++i) { if(!take(f,at,end,&c,1,pd)) return false; if(!c) return empty || i!=0; } return false; }
typedef struct rg { uint64_t at,n; } rg;
static bool nw_header(Abstractformat *f,const char *magic,uint16_t version,uint32_t *total,uint16_t *count,xx_pd_struct *pd) { uint8_t h[16]; if(stop(pd) || !pm_read(f,0,h,16) || xx_rt_memcmp(h,magic,4) || pm_be16(h+4)!=0xfeff || pm_be16(h+6)!=version || pm_be16(h+12)!=16 || !(*count=pm_be16(h+14)) || *count>1024 || (*total=pm_be32(h+8))<16 || *total>(uint64_t)pm_available(f)) return false; return true; }
static bool section(Abstractformat *f,uint64_t at,uint64_t total,const char *magic,uint32_t n,xx_pd_struct *pd) { uint8_t h[8]; return !stop(pd) && n>=8 && span(at,n,total) && pm_read(f,(int64_t)at,h,8) && !xx_rt_memcmp(h,magic,4) && pm_be32(h+4)==n; }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint32_t total,i,n; uint16_t count; uint64_t at=16; uint8_t h[76],p[8]; unsigned depth=0,gdepth=0,panes=0; bool layout=false,lastpane=false; char label[40];
    if(!nw_header(f,"RLYT",8,&total,&count,pd)) return false;
    for(i=0;i<count;++i) { if(!span(at,8,total) || !pm_read(f,(int64_t)at,h,8) || (n=pm_be32(h+4))<8 || (n&3) || !span(at,n,total)) return false;
      if(!xx_rt_memcmp(h,"lyt1",4)) { if(layout || panes || n!=20 || !pm_read(f,(int64_t)at,h,20) || h[8]>1 || !finite32(h+12,true) || !finite32(h+16,true) || (pm_be32(h+12)&0x80000000U) || (pm_be32(h+16)&0x80000000U) || !pm_be32(h+12) || !pm_be32(h+16)) return false; layout=true; }
      else if(!xx_rt_memcmp(h,"pan1",4)) { unsigned j; if(!layout || n!=76 || (!depth && panes) || !pm_read(f,(int64_t)at,h,76) || (h[8]&~7U) || h[9]>8 || !xx_rt_memchr(h+12,0,16) || !xx_rt_memchr(h+28,0,8)) return false; for(j=36;j<76;j+=4) if(!finite32(h+j,true)) return false; ++panes; lastpane=true; }
      else if(!xx_rt_memcmp(h,"pas1",4)) { if(n!=8 || !lastpane || ++depth>32) return false; lastpane=false; }
      else if(!xx_rt_memcmp(h,"pae1",4)) { if(n!=8 || !depth) return false; --depth; lastpane=false; }
      else if(!xx_rt_memcmp(h,"txl1",4) || !xx_rt_memcmp(h,"fnl1",4)) { uint32_t j,num; if(n<12 || !pm_read(f,(int64_t)at,h,12) || (num=pm_be16(h+8))>256 || !span(12,(uint64_t)num*8,n)) return false; for(j=0;j<num;++j) { uint64_t text; if(!pm_read(f,(int64_t)(at+12+j*8),p,8) || pm_be32(p)<num*8 || p[4]>1 || p[5] || p[6] || p[7]) return false; text=at+12+pm_be32(p); if(!cstring(f,&text,at+n,1024,false,pd)) return false; } }
      else if(!xx_rt_memcmp(h,"grp1",4)) { uint32_t num,j; if(n<28 || !pm_read(f,(int64_t)at,h,28) || !xx_rt_memchr(h+8,0,16) || !span(28,(uint64_t)(num=pm_be16(h+24))*16,n) || num>256) return false; for(j=0;j<num;++j) if(!pm_read(f,(int64_t)(at+28+j*16),h,16) || !xx_rt_memchr(h,0,16)) return false; }
      else if(!xx_rt_memcmp(h,"grs1",4)) { if(n!=8 || ++gdepth>32) return false; }
      else if(!xx_rt_memcmp(h,"gre1",4)) { if(n!=8 || !gdepth) return false; --gdepth; }
      else return false; if(!pm_read(f,(int64_t)at,h,4)) return false; xx_rt_snprintf(label,sizeof(label),"section-%c%c%c%c.bin",h[0],h[1],h[2],h[3]); if(!emit(f,s,label,at,n,total)) return false; at+=n; }
    if(!layout || !panes || depth || gdepth || at!=total) return false; s->size=total; return true;

}

void xx_nintendo_brlyt_init(xx_nintendo_brlyt *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BRLYT,"brlyt"); } }
xx_nintendo_brlyt *xx_nintendo_brlyt_create(xx_io_device *d,int64_t b) { xx_nintendo_brlyt *r=(xx_nintendo_brlyt *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_brlyt_init(r,d,b); return r; }
void xx_nintendo_brlyt_destroy(xx_nintendo_brlyt *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_brlyt_free(xx_nintendo_brlyt *r) { if(r) { xx_nintendo_brlyt_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_brlyt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_brlyt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
