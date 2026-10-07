/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://learn.microsoft.com/en-us/previous-versions/ms997538(v=msdn.10)
 * Independently implemented bounded parser; input ownership remains with caller.
 */
#include "xxfclib/formats/ico/xx_ico.h"
#include "xxfclib/formats/png/xx_png.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    (void)pd;

    uint8_t h[6],e[16],sig[40]; uint16_t count; uint32_t i; int64_t end;
    if(!pm_read(f,0,h,6) || xx_data_get_u16(h, 2, 0, false)!=0 || xx_data_get_u16(h+2, 2, 0, false)!=1 || (count=xx_data_get_u16(h+4, 2, 0, false))==0 || count>4096) return false;
    end=6+(int64_t)count*16;
    for(i=0;i<count;++i) {
        uint32_t size,off; bool png; char name[40];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,6+(int64_t)i*16,e,16) || e[3]) return false;
        size=xx_data_get_u32(e+8, 4, 0, false); off=xx_data_get_u32(e+12, 4, 0, false);
        if(off<6+(uint32_t)count*16 || size<12 || !pm_read(f,off,sig,size<40 ? size : 40)) return false;
        png=size>=8 && !xx_rt_memcmp(sig,"\x89PNG\r\n\x1a\n",8);
        if(png) {
            xx_png image;
            bool valid;
            if(size<33 || xx_data_get_u32(sig+8, 4, 0, true)!=13 || xx_rt_memcmp(sig+12,"IHDR",4) ||
                xx_data_get_u32(sig+16, 4, 0, true)!=(uint32_t)(e[0] ? e[0] : 256) || xx_data_get_u32(sig+20, 4, 0, true)!=(uint32_t)(e[1] ? e[1] : 256)) return false;
            xx_png_init(&image,f->device,f->base_address+off);
            valid=xx_png_handle_base_info(&image.format,pd) && image.format.format_size<=size;
            xx_png_destroy(&image);
            if(!valid) return false;
        } else {
            uint32_t dib=xx_data_get_u32(sig, 4, 0, false),width=e[0] ? e[0] : 256,height=e[1] ? e[1] : 256;
            uint32_t palette=0,masks=0,compression=0; uint16_t bpp;
            uint64_t needed;
            if((dib!=12 && dib!=40 && dib!=108 && dib!=124) || dib>size) return false;
            if(dib==12) {
                if(xx_data_get_u16(sig+4, 2, 0, false)!=width || xx_data_get_u16(sig+6, 2, 0, false)!=2*height || xx_data_get_u16(sig+8, 2, 0, false)!=1) return false;
                bpp=xx_data_get_u16(sig+10, 2, 0, false);
                if(bpp!=1 && bpp!=4 && bpp!=8 && bpp!=24) return false;
                if(bpp<=8) palette=(1U<<bpp)*3U;
            } else {
                uint32_t colors;
                if(xx_data_get_u32(sig+4, 4, 0, false)!=width || xx_data_get_u32(sig+8, 4, 0, false)!=2*height || xx_data_get_u16(sig+12, 2, 0, false)!=1) return false;
                bpp=xx_data_get_u16(sig+14, 2, 0, false); compression=xx_data_get_u32(sig+16, 4, 0, false); colors=xx_data_get_u32(sig+32, 4, 0, false);
                if(bpp!=1 && bpp!=4 && bpp!=8 && bpp!=16 && bpp!=24 && bpp!=32) return false;
                if(compression!=0 && compression!=3 && compression!=6) return false;
                if(compression && bpp!=16 && bpp!=32) return false;
                if(dib==40 && compression) masks=compression==6 ? 16 : 12;
                if(bpp<=8) {
                    if(colors>(1U<<bpp)) return false;
                    palette=(colors ? colors : 1U<<bpp)*4U;
                } else if(colors) {
                    if(colors>256) return false;
                    palette=colors*4U;
                }
            }
            needed=(uint64_t)dib+palette+masks+((width*bpp+31U)/32U)*4U*height;
            /* 32-bit alpha DIB icons may omit the legacy AND mask. Other
             * bit depths require the complete one-bit mask after XOR rows. */
            if(bpp!=32) needed+=((width+31U)/32U)*4U*height;
            if(needed>size) return false;
        }
        xx_rt_snprintf(name,sizeof(name),"image-%u.%s",(unsigned)i,png ? "png" : "dib");
        if(!pm_add(f,s,name,off,size)) return false;
        if((int64_t)off+size>end) end=(int64_t)off+size;
    }
    s->size=end; return true;

}
void xx_ico_init(xx_ico *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ICO,"ico"); } }
xx_ico *xx_ico_create(xx_io_device *d,int64_t b) { xx_ico *r=(xx_ico *)xx_mem_alloc(sizeof(*r)); if(r) xx_ico_init(r,d,b); return r; }
void xx_ico_destroy(xx_ico *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_ico_free(xx_ico *r) { if(r) { xx_ico_destroy(r); xx_mem_free(r); } }
bool xx_ico_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_ico_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
