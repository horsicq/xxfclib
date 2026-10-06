/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/godotengine/godot/master/scene/resources/compressed_texture.cpp
 * Godot4 GST2 version1 raw uncompressed L8/LA8/R8/RG8/RGB8/RGBA8 textures, up to16 complete mip levels and8192-pixel dimensions. Exports each stored mip plane with checked geometry/format length; PNG/WebP/Basis/GPU-compressed modes and rendering unsupported.
 */
#include "xxfclib/formats/godot_ctex/xx_godot_ctex.h"
#include "../xx_payload_members.h"

static bool span(uint64_t at,uint64_t n,uint64_t total) { return at<=total && n<=total-at; }
static bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m) { return n && m && a<b+m && b<a+n; }
static bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static bool emit(Abstractformat *f,pm_stream *s,const char *name,uint64_t at,uint64_t n,uint64_t total) {
    size_t i; if(!span(at,n,total) || total>(uint64_t)pm_available(f) || s->count>=4096) return false;
    for(i=0;i<s->count;++i) if(overlap(at,n,(uint64_t)(s->items[i].offset-f->base_address),(uint64_t)s->items[i].size)) return false;
    return pm_add(f,s,name,(int64_t)at,(int64_t)n);
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[52]; uint32_t w,height,mips,fmt,bpp,i,mw,mh; uint64_t at=52,n,total=(uint64_t)pm_available(f); char label[40];
    if(!pm_read(f,0,h,52) || xx_rt_memcmp(h,"GST2",4) || pm_le32(h+4)!=1 || pm_le32(h+36)) return false;
    w=pm_le16(h+40); height=pm_le16(h+42); mips=pm_le32(h+44); fmt=pm_le32(h+48);
    if(!w || !height || w>8192 || height>8192 || pm_le32(h+8)!=w || pm_le32(h+12)!=height || mips>15 || fmt>5 || (pm_le32(h+16)&~0x0d800000U) || (!!mips!=!!(pm_le32(h+16)&0x800000))) return false;
    for(i=24;i<36;++i) { if(h[i]) return false; } bpp=fmt==0 || fmt==2 ? 1 : fmt==1 || fmt==3 ? 2 : fmt==4 ? 3 : 4; mw=w; mh=height; if(mips) { uint32_t levels=0,a=w,b=height; while(a>1 || b>1) { if(a>1) a/=2; if(b>1) b/=2; ++levels; } if(mips!=levels) return false; }
    for(i=0;i<=mips;++i) { if(stop(pd) || (i<mips && mw==1 && mh==1)) return false; n=(uint64_t)mw*mh*bpp; xx_rt_snprintf(label,sizeof(label),"mip-%u.pixels",i);
      if(!emit(f,s,label,at,n,total)) { return false; } at+=n; if(mw>1) mw/=2; if(mh>1) mh/=2; }
    if(at!=total) { return false; } s->size=(int64_t)at; return true;

}

void xx_godot_ctex_init(xx_godot_ctex *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_GODOT_CTEX,"ctex"); } }
xx_godot_ctex *xx_godot_ctex_create(xx_io_device *d,int64_t b) { xx_godot_ctex *r=(xx_godot_ctex *)xx_mem_alloc(sizeof(*r)); if(r) xx_godot_ctex_init(r,d,b); return r; }
void xx_godot_ctex_destroy(xx_godot_ctex *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_godot_ctex_free(xx_godot_ctex *r) { if(r) { xx_godot_ctex_destroy(r); xx_mem_free(r); } }
bool xx_godot_ctex_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_godot_ctex_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
