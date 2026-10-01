/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide
 * DDS encoded mip surfaces: tightly packed RGB/luminance, DXT1/3/5, BC4/5 and selected DX10 BC/RGBA formats. No pixel decoding; unsupported formats or incomplete cube faces are rejected.
 */
#include "xxfclib/formats/dds/xx_dds.h"
#include "../xx_payload_members.h"

static uint16_t r16(const uint8_t *p,bool be) { return be ? pm_be16(p) : pm_le16(p); }
static uint32_t r32(const uint8_t *p,bool be) { return be ? pm_be32(p) : pm_le32(p); }
static uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[128],x[20]; uint32_t w,height,depth,levels,faces=1,layers=1,bpp=0,block=0,fmt,caps,i,j; int64_t at=128;
    if(!pm_read(f,0,h,128) || xx_rt_memcmp(h,"DDS ",4) || pm_le32(h+4)!=124 || pm_le32(h+76)!=32 ||
       (pm_le32(h+8)&0x1007)!=0x1007 || !(pm_le32(h+108)&0x1000)) return false;
    height=pm_le32(h+12); w=pm_le32(h+16); depth=pm_le32(h+24); levels=pm_le32(h+28); if(!levels) levels=1;
    caps=pm_le32(h+112); if(!w || !height || w>16384 || height>16384 || depth>16384 || levels>32) return false;
    if(caps&0x200000) { if(!depth || caps&0x200) return false; } else depth=1;
    if(caps&0x200) { if((caps&0xfc00)!=0xfc00) return false; faces=6; }
    fmt=pm_le32(h+84);
    if(pm_le32(h+80)&4) {
        if(fmt==0x31545844 || fmt==0x31495441 || fmt==0x55344342) block=8;
        else if(fmt==0x33545844 || fmt==0x35545844 || fmt==0x32495441 || fmt==0x55354342) block=16;
        else if(fmt==0x30315844) {
            if(!pm_read(f,128,x,20) || pm_le32(x+4)!=3 || !pm_le32(x+12) || pm_le32(x+12)>4096) return false;
            layers=pm_le32(x+12); faces=pm_le32(x+8)&4 ? 6 : 1; depth=1; at=148;
            fmt=pm_le32(x); if(fmt==28 || fmt==29 || fmt==87 || fmt==91) bpp=32;
            else if(fmt==61) bpp=8;
            else if(fmt==71 || fmt==72 || fmt==80 || fmt==81) block=8;
            else if(fmt==74 || fmt==75 || fmt==77 || fmt==78 || fmt==83 || fmt==84 || fmt==98 || fmt==99) block=16;
            else return false;
        } else return false;
    } else {
        uint32_t mask,red=pm_le32(h+92),green=pm_le32(h+96),blue=pm_le32(h+100),alpha=pm_le32(h+104);
        bpp=pm_le32(h+88); if(!(pm_le32(h+80)&(0x40|0x20000)) || (bpp!=8 && bpp!=16 && bpp!=24 && bpp!=32)) return false;
        mask=red|green|blue|alpha;
        if(!red || ((pm_le32(h+80)&0x40) && (!green || !blue)) ||
           (red&green) || (red&blue) || (green&blue) || (alpha&(red|green|blue)) ||
           ((pm_le32(h+80)&1) && !alpha) || (bpp<32 && (mask>>bpp))) return false;
    }
    if(bpp && (pm_le32(h+8)&8) && pm_le32(h+20)!=(uint64_t)w*(bpp/8)) return false;
    if((uint64_t)layers*faces*levels>65536) return false;
    for(j=0;j<layers*faces;++j) for(i=0;i<levels;++i) {
        uint32_t mw=w>>i,mh=height>>i,md=depth>>i; uint64_t size; char label[40];
        if(pd && xx_pd_is_stopped(pd)) return false; if(!mw) mw=1; if(!mh) mh=1; if(!md) md=1;
        size=block ? ((mw+3ULL)/4)*((mh+3ULL)/4)*md*block : (uint64_t)mw*mh*md*(bpp/8);
        xx_rt_snprintf(label,sizeof(label),"surface-%u-mip-%u.bin",(unsigned)j,(unsigned)i);
        if(size>INT64_MAX || !pm_add(f,s,label,at,(int64_t)size)) return false; at+=(int64_t)size;
    }
    s->size=at; return true;
}

void xx_dds_init(xx_dds *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_DDS,"dds"); } }
xx_dds *xx_dds_create(xx_io_device *d,int64_t b) { xx_dds *r=(xx_dds *)xx_mem_alloc(sizeof(*r)); if(r) xx_dds_init(r,d,b); return r; }
void xx_dds_destroy(xx_dds *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_dds_free(xx_dds *r) { if(r) { xx_dds_destroy(r); xx_mem_free(r); } }
bool xx_dds_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_dds_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
