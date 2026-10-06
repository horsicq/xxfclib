/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/nickworonekin/puyotools/master/src/PuyoTools.Core/Textures/Pvr/PvrTextureEncoder.cs
 * Dreamcast PVRT container, direct 16-bit ARGB1555/RGB565/ARGB4444 texture data, rectangular or square/rectangular twiddled single level. Encoded bytes exported; GBIX wrapper, VQ/palette/mipmaps, RLE and pixel rendering unsupported. Distinct from PVR3.
 */
#include "xxfclib/formats/sega_pvr2/xx_sega_pvr2.h"
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

    uint8_t h[16]; uint32_t w,he; uint64_t n;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"PVRT",4) || h[8]>2 || pm_le16(h+10)) return false;
    w=pm_le16(h+12); he=pm_le16(h+14); if(!w || !he || w>8192 || he>8192 || (h[9]!=1 && h[9]!=9 && h[9]!=13)) return false;
    if(h[9]!=9 && ((w&(w-1)) || (he&(he-1)) || (h[9]==1 && w!=he))) return false;
    n=(uint64_t)w*he*2; if(pm_le32(h+4)!=n+8 || !span(16,n,(uint64_t)pm_available(f)) || (pd && xx_pd_is_stopped(pd))) return false;
    s->size=(int64_t)(16+n); return emit(f,s,"texture.bin",16,n,(uint64_t)s->size);

}

void xx_sega_pvr2_init(xx_sega_pvr2 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SEGA_PVR2,"pvr"); } }
xx_sega_pvr2 *xx_sega_pvr2_create(xx_io_device *d,int64_t b) { xx_sega_pvr2 *r=(xx_sega_pvr2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_sega_pvr2_init(r,d,b); return r; }
void xx_sega_pvr2_destroy(xx_sega_pvr2 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sega_pvr2_free(xx_sega_pvr2 *r) { if(r) { xx_sega_pvr2_destroy(r); xx_mem_free(r); } }
bool xx_sega_pvr2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sega_pvr2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
