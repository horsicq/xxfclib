/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://psx-spx.consoledev.net/cdromfileformats/#cdrom-file-video-tim-textures
 * PlayStation TIM version0 single-image files, 4/8-bit indexed and direct 16/24-bit pixels. Exports CLUT and encoded image planes with exact rectangle lengths; no rendering, mixed-depth or malformed game-specific variants.
 */
#include "xxfclib/formats/sony_tim/xx_sony_tim.h"
#include "../xx_payload_members.h"

static XXFC_MAYBE_UNUSED uint64_t g64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
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

    uint8_t h[8],e[12]; uint32_t flags,i,blocks; uint64_t at=8,total=(uint64_t)pm_available(f);
    if(!pm_read(f,0,h,8) || pm_le32(h)!=16) { return false; } flags=pm_le32(h+4);
    if(flags&~15U || (flags&7)>3 || (((flags&7)<2)!=!!(flags&8))) { return false; } blocks=(flags&8) ? 2 : 1;
    for(i=0;i<blocks;++i) { uint64_t n,bytes; uint32_t w,he,x,y;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)at,e,12)) return false;
        n=pm_le32(e); x=pm_le16(e+4); y=pm_le16(e+6); w=pm_le16(e+8); he=pm_le16(e+10); bytes=(uint64_t)w*he*2;
        if(!w || !he || x+w>1024 || y+he>512 || n!=bytes+12 || !span(at,n,total)) return false;
        if(i==0 && (flags&8) && w!=((flags&7)==0 ? 16U : 256U)) return false;
        if(!emit(f,s,(i==0 && (flags&8)) ? "clut.bin" : "pixels.bin",at+12,bytes,total)) { return false; } at+=n; }
    s->size=(int64_t)at; return true;

}

void xx_sony_tim_init(xx_sony_tim *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SONY_TIM,"tim"); } }
xx_sony_tim *xx_sony_tim_create(xx_io_device *d,int64_t b) { xx_sony_tim *r=(xx_sony_tim *)xx_mem_alloc(sizeof(*r)); if(r) xx_sony_tim_init(r,d,b); return r; }
void xx_sony_tim_destroy(xx_sony_tim *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sony_tim_free(xx_sony_tim *r) { if(r) { xx_sony_tim_destroy(r); xx_mem_free(r); } }
bool xx_sony_tim_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sony_tim_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
