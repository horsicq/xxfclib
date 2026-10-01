/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/exelix11/SwitchThemeInjector/master/SwitchThemesNX/source/SwitchThemesCommon/Layouts/Bflyt/Bflyt.cpp
 * FLYT version0x8040000, sequential bounded layout sections, balanced pane/group markers and bounded texture/font-name tables. Exports encoded sections; no UI execution, texture loading, material interpretation or rendering.
 */
#include "xxfclib/formats/nintendo_bflyt/xx_nintendo_bflyt.h"
#include "../xx_payload_members.h"

static uint16_t g16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static uint32_t g32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i;
    if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool zname(Abstractformat *f,uint64_t at,uint64_t end,bool empty) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return empty || i!=0; } return false;
}
static bool bom(const uint8_t *p,bool *be) { *be=p[0]==0xfe && p[1]==0xff; return *be || (p[0]==0xff && p[1]==0xfe); }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[20],b[12]; uint32_t total,count,i,version; uint64_t at=20; bool be,layout=false; unsigned panes=0,groups=0;
    if(!pm_read(f,0,h,20) || xx_rt_memcmp(h,"FLYT",4) || !bom(h+4,&be) || g16(h+6,be)!=20) return false;
    version=g32(h+8,be); total=g32(h+12,be); count=g32(h+16,be);
    if(!(version==0x8040000) || total<20 || total>(uint64_t)pm_available(f) || !count || count>4096) return false;
    for(i=0;i<count;++i) {
        uint32_t n,j; char label[40];
        if((pd && xx_pd_is_stopped(pd)) || !span(at,8,total) || !pm_read(f,(int64_t)at,b,8)) return false; n=g32(b+4,be);
        if(n<8 || !span(at,n,total)) return false;
        for(j=0;j<4;++j) if(b[j]<33 || b[j]>126) return false;
        if(!xx_rt_memcmp(b,"lyt1",4)) { if(layout || i || n<20) return false; layout=true; }
        else if(!xx_rt_memcmp(b,"pas1",4)) { if(n!=8 || ++panes>256) return false; }
        else if(!xx_rt_memcmp(b,"pae1",4)) { if(n!=8 || !panes) return false; --panes; }
        else if(!xx_rt_memcmp(b,"grs1",4)) { if(n!=8 || ++groups>256) return false; }
        else if(!xx_rt_memcmp(b,"gre1",4)) { if(n!=8 || !groups) return false; --groups; }
        else if(!xx_rt_memcmp(b,"txl1",4) || !xx_rt_memcmp(b,"fnl1",4)) {
            uint32_t entries,k; uint8_t p[4]; if(n<12 || !pm_read(f,(int64_t)at+8,p,4)) return false; entries=g32(p,be);
            if(entries>4096 || !span(12,(uint64_t)entries*4,n)) return false;
            for(k=0;k<entries;++k) { uint64_t name; if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)at+12+k*4,p,4)) return false;
                name=12U+(uint64_t)g32(p,be); if(name<12U+entries*4 || !span(name,1,n) || !zname(f,at+name,at+n,false)) return false; }
        }
        xx_rt_snprintf(label,sizeof(label),"section-%c%c%c%c.bin",b[0],b[1],b[2],b[3]); if(!emit(f,s,label,at,n,total)) return false; at+=n;
    }
    s->size=total; return layout && !panes && !groups && at==total;

}

void xx_nintendo_bflyt_init(xx_nintendo_bflyt *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_BFLYT,"bflyt"); } }
xx_nintendo_bflyt *xx_nintendo_bflyt_create(xx_io_device *d,int64_t b) { xx_nintendo_bflyt *r=(xx_nintendo_bflyt *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_bflyt_init(r,d,b); return r; }
void xx_nintendo_bflyt_destroy(xx_nintendo_bflyt *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_bflyt_free(xx_nintendo_bflyt *r) { if(r) { xx_nintendo_bflyt_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_bflyt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_bflyt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
