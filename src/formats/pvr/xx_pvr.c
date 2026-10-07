/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://docs.imgtec.com/specifications/pvr-file-format-specification/html/topics/pvr-header-format.html
 * PVR3 both byte orders: byte-aligned channel formats and selected ETC/BC block formats, metadata and encoded mip groups. PVRTC/ASTC/Basis and pixel decoding are not supported.
 */
#include "xxfclib/formats/pvr/xx_pvr.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t r16(const uint8_t *p,bool be) { return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static uint32_t r32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true)<<32)|xx_data_get_u32(p+4, 4, 0, true) : ((uint64_t)xx_data_get_u32(p+4, 4, 0, false)<<32)|xx_data_get_u32(p, 4, 0, false); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[52],b[12]; bool be; uint32_t width,height,depth,layers,faces,levels,meta,bits=0,block=0,i; uint64_t pixel; int64_t at,metaend;
    if(!pm_read(f,0,h,52)) { return false; } be=xx_data_get_u32(h, 4, 0, true)==0x03525650;
    if(!be && xx_data_get_u32(h, 4, 0, false)!=0x03525650) return false;
    pixel=r64(h+8,be); height=r32(h+24,be); width=r32(h+28,be); depth=r32(h+32,be); layers=r32(h+36,be); faces=r32(h+40,be); levels=r32(h+44,be); meta=r32(h+48,be);
    if((r32(h+4,be)&~2U) || r32(h+16,be)>1 || r32(h+20,be)>13 || !width || !height || !depth || !layers || !levels ||
       width>16384 || height>16384 || depth>16384 || layers>4096 || (faces!=1 && faces!=6) || levels>32 || meta>16U*1024U*1024U) return false;
    if(pixel>>32) {
        uint32_t channels=(uint32_t)pixel,rates=(uint32_t)(pixel>>32),j;
        for(j=0;j<4;++j) { uint32_t c=(channels>>(j*8))&255,n=(rates>>(j*8))&255; if((c==0)!=(n==0) || n>32) return false; bits+=n; }
        if(!bits || bits>128 || (bits&7)) return false;
    } else if(pixel==6 || pixel==7 || pixel==12 || pixel==22 || pixel==24 || pixel==25) block=8;
    else if(pixel==8 || pixel==9 || pixel==10 || pixel==11 || pixel==13 || pixel==14 || pixel==15 || pixel==23 || pixel==26) block=16;
    else return false;
    at=52; metaend=at+meta; if(metaend>pm_available(f)) return false;
    while(at<metaend) {
        uint32_t n; if((pd && xx_pd_is_stopped(pd)) || metaend-at<12 || !pm_read(f,at,b,12)) return false;
        n=r32(b+8,be); if(n>metaend-at-12 || !pm_add(f,s,"metadata.bin",at,12+(int64_t)n)) return false; at+=12+(int64_t)n;
    }
    for(i=0;i<levels;++i) {
        uint32_t w=width>>i,hh=height>>i,d=depth>>i; uint64_t n; char label[40];
        if(pd && xx_pd_is_stopped(pd)) { return false; } if(!w) w=1; if(!hh) hh=1; if(!d) d=1;
        n=(block ? ((w+3ULL)/4)*((hh+3ULL)/4)*block : (uint64_t)w*hh*(bits/8))*d*layers*faces;
        xx_rt_snprintf(label,sizeof(label),"mip-%u.bin",(unsigned)i);
        if(n>INT64_MAX || !pm_add(f,s,label,at,(int64_t)n)) { return false; } at+=(int64_t)n;
    }
    s->size=at; return true;
}

void xx_pvr_init(xx_pvr *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PVR,"pvr"); } }
xx_pvr *xx_pvr_create(xx_io_device *d,int64_t b) { xx_pvr *r=(xx_pvr *)xx_mem_alloc(sizeof(*r)); if(r) xx_pvr_init(r,d,b); return r; }
void xx_pvr_destroy(xx_pvr *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_pvr_free(xx_pvr *r) { if(r) { xx_pvr_destroy(r); xx_mem_free(r); } }
bool xx_pvr_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_pvr_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
