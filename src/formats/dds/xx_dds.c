/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide
 * DDS encoded mip surfaces: tightly packed RGB/luminance, DXT1/3/5, BC4/5 and selected DX10 BC/RGBA formats. No pixel decoding; unsupported formats or incomplete cube faces are rejected.
 */
#include "xxfclib/formats/dds/xx_dds.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t r16(const uint8_t *p,bool be) { return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static XXFC_MAYBE_UNUSED uint32_t r32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
static XXFC_MAYBE_UNUSED uint64_t r64(const uint8_t *p,bool be) { return be ? ((uint64_t)xx_data_get_u32(p, 4, 0, true)<<32)|xx_data_get_u32(p+4, 4, 0, true) : ((uint64_t)xx_data_get_u32(p+4, 4, 0, false)<<32)|xx_data_get_u32(p, 4, 0, false); }
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[128],x[20]; uint32_t w,height,depth,levels,faces=1,layers=1,bpp=0,block=0,fmt,caps,i,j; int64_t at=128;
    if(!pm_read(f,0,h,128) || xx_rt_memcmp(h,"DDS ",4) || xx_data_get_u32(h+4, 4, 0, false)!=124 || xx_data_get_u32(h+76, 4, 0, false)!=32 ||
       (xx_data_get_u32(h+8, 4, 0, false)&0x1007)!=0x1007 || !(xx_data_get_u32(h+108, 4, 0, false)&0x1000)) return false;
    height=xx_data_get_u32(h+12, 4, 0, false); w=xx_data_get_u32(h+16, 4, 0, false); depth=xx_data_get_u32(h+24, 4, 0, false); levels=xx_data_get_u32(h+28, 4, 0, false); if(!levels) levels=1;
    caps=xx_data_get_u32(h+112, 4, 0, false); if(!w || !height || w>16384 || height>16384 || depth>16384 || levels>32) return false;
    if(caps&0x200000) { if(!depth || caps&0x200) return false; } else depth=1;
    if(caps&0x200) { if((caps&0xfc00)!=0xfc00) return false; faces=6; }
    fmt=xx_data_get_u32(h+84, 4, 0, false);
    if(xx_data_get_u32(h+80, 4, 0, false)&4) {
        if(fmt==0x31545844 || fmt==0x31495441 || fmt==0x55344342) block=8;
        else if(fmt==0x33545844 || fmt==0x35545844 || fmt==0x32495441 || fmt==0x55354342) block=16;
        else if(fmt==0x30315844) {
            if(!pm_read(f,128,x,20) || xx_data_get_u32(x+4, 4, 0, false)!=3 || !xx_data_get_u32(x+12, 4, 0, false) || xx_data_get_u32(x+12, 4, 0, false)>4096) return false;
            layers=xx_data_get_u32(x+12, 4, 0, false); faces=xx_data_get_u32(x+8, 4, 0, false)&4 ? 6 : 1; depth=1; at=148;
            fmt=xx_data_get_u32(x, 4, 0, false); if(fmt==28 || fmt==29 || fmt==87 || fmt==91) bpp=32;
            else if(fmt==61) bpp=8;
            else if(fmt==71 || fmt==72 || fmt==80 || fmt==81) block=8;
            else if(fmt==74 || fmt==75 || fmt==77 || fmt==78 || fmt==83 || fmt==84 || fmt==98 || fmt==99) block=16;
            else return false;
        } else return false;
    } else {
        uint32_t mask,red=xx_data_get_u32(h+92, 4, 0, false),green=xx_data_get_u32(h+96, 4, 0, false),blue=xx_data_get_u32(h+100, 4, 0, false),alpha=xx_data_get_u32(h+104, 4, 0, false);
        bpp=xx_data_get_u32(h+88, 4, 0, false); if(!(xx_data_get_u32(h+80, 4, 0, false)&(0x40|0x20000)) || (bpp!=8 && bpp!=16 && bpp!=24 && bpp!=32)) return false;
        mask=red|green|blue|alpha;
        if(!red || ((xx_data_get_u32(h+80, 4, 0, false)&0x40) && (!green || !blue)) ||
           (red&green) || (red&blue) || (green&blue) || (alpha&(red|green|blue)) ||
           ((xx_data_get_u32(h+80, 4, 0, false)&1) && !alpha) || (bpp<32 && (mask>>bpp))) return false;
    }
    if(bpp && (xx_data_get_u32(h+8, 4, 0, false)&8) && xx_data_get_u32(h+20, 4, 0, false)!=(uint64_t)w*(bpp/8)) return false;
    if((uint64_t)layers*faces*levels>65536) return false;
    for(j=0;j<layers*faces;++j) for(i=0;i<levels;++i) {
        uint32_t mw=w>>i,mh=height>>i,md=depth>>i; uint64_t size; char label[40];
        if(pd && xx_pd_is_stopped(pd)) { return false; } if(!mw) mw=1; if(!mh) mh=1; if(!md) md=1;
        size=block ? ((mw+3ULL)/4)*((mh+3ULL)/4)*md*block : (uint64_t)mw*mh*md*(bpp/8);
        xx_rt_snprintf(label,sizeof(label),"surface-%u-mip-%u.bin",(unsigned)j,(unsigned)i);
        if(size>INT64_MAX || !pm_add(f,s,label,at,(int64_t)size)) { return false; } at+=(int64_t)size;
    }
    s->size=at; return true;
}

void xx_dds_init(xx_dds *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_DDS,"dds"); } }
xx_dds *xx_dds_create(xx_io_device *d,int64_t b) { xx_dds *r=(xx_dds *)xx_mem_alloc(sizeof(*r)); if(r) xx_dds_init(r,d,b); return r; }
void xx_dds_destroy(xx_dds *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_dds_free(xx_dds *r) { if(r) { xx_dds_destroy(r); xx_mem_free(r); } }
bool xx_dds_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_dds_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
