/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/d0k3/GodMode9/master/arm9/source/game/ncsd.h
 * Full-extent NCSD gamecards with up to eight unencrypted NCCH partitions. Exports whole declared partition components; NAND filesystem partitions and encrypted NCCH rejected. No cartridge signature verification.
 */
#include "xxfclib/formats/nintendo_ncsd/xx_nintendo_ncsd.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint64_t g64(const uint8_t *p,bool be) { return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true)<<32)|xx_data_get_u32(p+4, 4, 0, true) : ((uint64_t)xx_data_get_u32(p+4, 4, 0, false)<<32)|xx_data_get_u32(p, 4, 0, false); }
static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i;
    if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) { uint64_t a=(uint64_t)(s->items[i].offset-f->base_address),b=(uint64_t)s->items[i].size;
        if(n && b && at<a+b && a<at+n) return false; }
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static XXFC_MAYBE_UNUSED bool zname(Abstractformat *f,uint64_t at,uint64_t end) {
    uint8_t c; uint64_t i; if(at>=end || end>(uint64_t)pm_available(f)) return false;
    for(i=0;i<4096 && at+i<end;++i) { if(!pm_read(f,(int64_t)(at+i),&c,1)) return false; if(!c) return i!=0; } return false;
}


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[512],p[512]; uint64_t total; unsigned i; char label[40];
    if(!pm_read(f,0,h,512) || xx_rt_memcmp(h+256,"NCSD",4)) return false;
    total=(uint64_t)xx_data_get_u32(h+0x104, 4, 0, false)*512; if(total<0x4000 || total>(uint64_t)pm_available(f)) return false;
    for(i=0;i<8;++i) { uint64_t at=(uint64_t)xx_data_get_u32(h+0x120+i*8, 4, 0, false)*512,n=(uint64_t)xx_data_get_u32(h+0x124+i*8, 4, 0, false)*512,inner;
        if(pd && xx_pd_is_stopped(pd)) return false;
        if(!n) { if(at) return false; continue; }
        if(h[0x110+i] || at<0x4000 || n<512 || !span(at,n,total) || !pm_read(f,(int64_t)at,p,512) || xx_rt_memcmp(p+256,"NCCH",4) || !(p[0x18f]&4) || (p[0x18f]&0x21) || p[0x18e]) return false;
        inner=(uint64_t)xx_data_get_u32(p+0x104, 4, 0, false)*512; if(inner<512 || inner>n || xx_data_get_u16(p+0x112, 2, 0, false)>2) return false;
        xx_rt_snprintf(label,sizeof(label),"partition-%u.ncch",i); if(!emit(f,s,label,at,n,total)) return false; }
    s->size=(int64_t)total; return s->count!=0;

}

void xx_nintendo_ncsd_init(xx_nintendo_ncsd *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_NCSD,"3ds"); } }
xx_nintendo_ncsd *xx_nintendo_ncsd_create(xx_io_device *d,int64_t b) { xx_nintendo_ncsd *r=(xx_nintendo_ncsd *)xx_mem_alloc(sizeof(*r)); if(r) xx_nintendo_ncsd_init(r,d,b); return r; }
void xx_nintendo_ncsd_destroy(xx_nintendo_ncsd *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_ncsd_free(xx_nintendo_ncsd *r) { if(r) { xx_nintendo_ncsd_destroy(r); xx_mem_free(r); } }
bool xx_nintendo_ncsd_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_ncsd_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
