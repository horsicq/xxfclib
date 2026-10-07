/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/microsoft/DirectXTK/main/Audio/WaveBankReader.cpp
 * XACT wave-bank header version44, little-endian noncompact buffered banks with up to1024 PCM8/PCM16 entries and optional fixed-size names. Validates five disjoint segments, mini-format, duration/loop and wave extents. Exports encoded PCM entries; streaming/compact/ADPCM/XMA/WMA/seek tables and playback unsupported.
 */
#include "xxfclib/formats/microsoft_xwb/xx_microsoft_xwb.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static XXFC_MAYBE_UNUSED uint16_t g16(const uint8_t *p,bool be) { return be ? xx_data_get_u16(p, 2, 0, true) : xx_data_get_u16(p, 2, 0, false); }
static XXFC_MAYBE_UNUSED uint32_t g32(const uint8_t *p,bool be) { return be ? xx_data_get_u32(p, 4, 0, true) : xx_data_get_u32(p, 4, 0, false); }
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
static XXFC_MAYBE_UNUSED bool bom(const uint8_t *p,bool *be) { *be=p[0]==0xfe && p[1]==0xff; return *be || (p[0]==0xff && p[1]==0xfe); }


static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[52],b[96],e[24],name[64]; uint32_t offs[5],lens[5],count,flags,alignment,i,j; uint64_t total=52; char label[40];
    if(!pm_read(f,0,h,52) || xx_rt_memcmp(h,"WBND",4) || xx_data_get_u32(h+8, 4, 0, false)!=44) return false;
    for(i=0;i<5;++i) { offs[i]=xx_data_get_u32(h+12+i*8, 4, 0, false); lens[i]=xx_data_get_u32(h+16+i*8, 4, 0, false);
        if(!lens[i]) { if(offs[i]) return false; continue; }
        if(offs[i]<52 || (offs[i]&3) || !span(offs[i],lens[i],(uint64_t)pm_available(f))) return false;
        for(j=0;j<i;++j) if(overlap(offs[i],lens[i],offs[j],lens[j])) return false;
        if((uint64_t)offs[i]+lens[i]>total) total=(uint64_t)offs[i]+lens[i]; }
    if(lens[0]!=96 || lens[2] || !pm_read(f,offs[0],b,96)) return false;
    flags=xx_data_get_u32(b, 4, 0, false); count=xx_data_get_u32(b+4, 4, 0, false); alignment=xx_data_get_u32(b+80, 4, 0, false);
    if((flags&~0x10000U) || !count || count>1024 || xx_data_get_u32(b+72, 4, 0, false)!=24 || alignment<4 || alignment>65536 || (alignment&(alignment-1)) || lens[1]!=(uint64_t)count*24 || !lens[4] || !zname(f,(uint64_t)offs[0]+8,(uint64_t)offs[0]+72,false)) return false;
    if(flags&0x10000) { if(xx_data_get_u32(b+76, 4, 0, false)!=64 || lens[3]!=(uint64_t)count*64) return false; }
    else if(lens[3] || xx_data_get_u32(b+76, 4, 0, false)) return false;
    for(i=0;i<count;++i) { uint32_t fmt,channels,rate,block,bits,duration,at,n,loop,loopn;
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,(int64_t)offs[1]+i*24,e,24)) return false;
        fmt=xx_data_get_u32(e+4, 4, 0, false); channels=(fmt>>2)&7; rate=(fmt>>5)&0x3ffff; block=(fmt>>23)&255; bits=(fmt>>31) ? 16 : 8;
        duration=xx_data_get_u32(e, 4, 0, false)>>4; at=xx_data_get_u32(e+8, 4, 0, false); n=xx_data_get_u32(e+12, 4, 0, false); loop=xx_data_get_u32(e+16, 4, 0, false); loopn=xx_data_get_u32(e+20, 4, 0, false);
        if((fmt&3) || !channels || channels>6 || !rate || rate>192000 || block!=channels*(bits/8) || !duration || !n || (uint64_t)duration*block!=n || at%alignment || !span(at,n,lens[4]) || loop>duration || loopn>duration-loop) return false;
        if(flags&0x10000) { if(!pm_read(f,(int64_t)offs[3]+i*64,name,64) || !xx_rt_memchr(name,0,64)) return false; }
        xx_rt_snprintf(label,sizeof(label),"wave-%u.pcm",i); if(!emit(f,s,label,(uint64_t)offs[4]+at,n,total)) return false;
    }
    s->size=(int64_t)total; return true;

}

void xx_microsoft_xwb_init(xx_microsoft_xwb *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MICROSOFT_XWB,"xwb"); } }
xx_microsoft_xwb *xx_microsoft_xwb_create(xx_io_device *d,int64_t b) { xx_microsoft_xwb *r=(xx_microsoft_xwb *)xx_mem_alloc(sizeof(*r)); if(r) xx_microsoft_xwb_init(r,d,b); return r; }
void xx_microsoft_xwb_destroy(xx_microsoft_xwb *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_microsoft_xwb_free(xx_microsoft_xwb *r) { if(r) { xx_microsoft_xwb_destroy(r); xx_mem_free(r); } }
bool xx_microsoft_xwb_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_microsoft_xwb_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
