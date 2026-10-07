/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://learn.microsoft.com/en-us/typography/opentype/spec/otff
 * Single SFNT fonts (TrueType/OpenType); validates directory and table checksums, exports tables; no TTC collection or glyph rendering.
 */
#include "xxfclib/formats/sfnt/xx_sfnt.h"
#include "../xx_payload_members.h"
#include "xx_font_table_impl.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[12],e[16]; uint16_t count,power=1,log=0; uint32_t previous=0,i,j; int64_t end;
    uint32_t offsets[4096],sizes[4096];
    if(!pm_read(f,0,h,12) || !font_flavor(xx_data_get_u32(h, 4, 0, true))) return false;
    count=xx_data_get_u16(h+4, 2, 0, true); if(!count || count>4095) return false;
    while((unsigned)power*2<=count) { power*=2; ++log; }
    if(xx_data_get_u16(h+6, 2, 0, true)!=power*16U || xx_data_get_u16(h+8, 2, 0, true)!=log || xx_data_get_u16(h+10, 2, 0, true)!=count*16U-power*16U) return false;
    end=12+(int64_t)count*16;
    for(i=0;i<count;++i) {
        uint32_t tag,off,size; char name[64];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,12+(int64_t)i*16,e,16) || !font_tag(e)) return false;
        tag=xx_data_get_u32(e, 4, 0, true); off=xx_data_get_u32(e+8, 4, 0, true); size=xx_data_get_u32(e+12, 4, 0, true);
        if((i && tag<=previous) || off%4 || off<12U+(uint32_t)count*16U || off>pm_available(f) || size>(uint64_t)(pm_available(f)-off)) return false;
        previous=tag;
        for(j=0;j<i;++j) if(size && sizes[j] && off<(uint64_t)offsets[j]+sizes[j] && offsets[j]<(uint64_t)off+size) return false;
        offsets[i]=off; sizes[i]=size;
        xx_rt_snprintf(name,sizeof(name),"table-%08x.bin",xx_data_get_u32(e, 4, 0, true));
        if(!font_range(f,s,off,size,size,name,0,xx_data_get_u32(e+4, 4, 0, true),true,pd)) return false;
        if((int64_t)off+size>end) end=(int64_t)off+size;
    }
    if(end>64*1024*1024) return false;
    s->size=end; return true;
}

void xx_sfnt_init(xx_sfnt *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFNT,"sfnt"); } }
xx_sfnt *xx_sfnt_create(xx_io_device *d,int64_t b) { xx_sfnt *r=(xx_sfnt *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfnt_init(r,d,b); return r; }
void xx_sfnt_destroy(xx_sfnt *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfnt_free(xx_sfnt *r) { if(r) { xx_sfnt_destroy(r); xx_mem_free(r); } }
bool xx_sfnt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfnt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
