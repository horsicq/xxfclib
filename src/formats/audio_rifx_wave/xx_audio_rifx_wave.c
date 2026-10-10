/* SPDX-License-Identifier: MIT.
 * Standard RIFX/WAVE big-endian chunk framing and WAVEFORMAT:
 * https://github.com/vgmstream/vgmstream/blob/master/src/meta/wwise.c
 * https://github.com/vgmstream/vgmstream/blob/master/src/meta/riff.c
 * This reader exposes encoded chunk bytes; it does not decode audio.
 */
#include "xxfclib/formats/audio_rifx_wave/xx_audio_rifx_wave.h"
#include "../common/xx_binary_cursor.h"
#ifndef AUDIO_RIFX_WAVE
#define XX_FILE_TYPE_AUDIO_RIFX_WAVE ((xx_file_type_t)1513)
#endif
static bool printable(const uint8_t*p) {
    unsigned i;for(i=0;i<4;++i)if(p[i]<32 || p[i]>126)return false;
    return true;
}
static bool fmt_valid(const uint8_t*p,uint32_t n) {
    uint32_t tag,ch,rate,bytes,align,bits,expected;
    if(n<16 || n>4096 || n==17)return false;
    tag=xx_data_get_u16(p, 2, 0, true);ch=xx_data_get_u16(p+2, 2, 0, true);rate=xx_data_get_u32(p+4, 4, 0, true);
    bytes=xx_data_get_u32(p+8, 4, 0, true);align=xx_data_get_u16(p+12, 2, 0, true);bits=xx_data_get_u16(p+14, 2, 0, true);
    if(!tag || !ch || ch>64 || !rate || rate>384000 ||
       !bytes || !align || bits>64)return false;
    if(n>=18 && xx_data_get_u16(p+16, 2, 0, true)>n-18)return false;
    if(tag==1 || tag==3) {
        if((tag==1 && bits!=8 && bits!=16 && bits!=24 && bits!=32) ||
           (tag==3 && bits!=32 && bits!=64))return false;
        expected=ch*((bits+7U)/8U);
        if(align!=expected || (uint64_t)rate*align!=bytes)return false;
    }
    return true;
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {
    uint8_t h[12],c[8],fmt[4096];uint64_t p,total;
    uint32_t fmt_align=0,fmt_tag=0;unsigned chunks=0,has_fmt=0,has_data=0;
    int64_t available=pm_available(f);
    if(binary_stop(pd)||available<36 || !pm_read(f,0,h,12) ||
       xx_rt_memcmp(h,"RIFX",4) || xx_rt_memcmp(h+8,"WAVE",4))
        return false;
    total=8U+(uint64_t)xx_data_get_u32(h+4, 4, 0, true);
    if(total<36 || total>(uint64_t)available ||
       !pm_add(f,s,"rifx-header.bin",0,12))return false;
    p=12;
    while(p<total) {
        uint32_t size;uint64_t next;char label[48],tag[5];unsigned i;
        if(binary_stop(pd)||++chunks>4096 || !binary_range(p,8,total) ||
           !pm_read(f,(int64_t)p,c,8) || !printable(c))return false;
        size=xx_data_get_u32(c+4, 4, 0, true);next=p+8U+(uint64_t)size+(size&1U);
        if(next>total)return false;
        for(i=0;i<4;++i)
            tag[i]=(c[i]==' ' || c[i]=='/' || c[i]=='\\' || c[i]==':' ||
                    c[i]=='<' || c[i]=='>' || c[i]=='|' || c[i]=='?' ||
                    c[i]=='*')?'_':(char)c[i];
        tag[4]=0;
        if(!xx_rt_memcmp(c,"fmt ",4)) {
            if(has_fmt || size>sizeof(fmt) || size<16 ||
               !pm_read(f,(int64_t)p+8,fmt,size) || !fmt_valid(fmt,size))
                return false;
            fmt_tag=xx_data_get_u16(fmt, 2, 0, true);fmt_align=xx_data_get_u16(fmt+12, 2, 0, true);
            has_fmt=1;xx_rt_snprintf(label,sizeof(label),"format-descriptor.bin");
        } else if(!xx_rt_memcmp(c,"data",4)) {
            if(!size)return false;
            ++has_data;xx_rt_snprintf(label,sizeof(label),"audio-data.bin");
        } else xx_rt_snprintf(label,sizeof(label),"%s.bin",tag);
        if(!pm_add(f,s,label,(int64_t)p+8,size))return false;
        p=next;
    }
    if(p!=total || !has_fmt || !has_data)return false;
    if(fmt_tag==1 || fmt_tag==3) {
        unsigned i;for(i=0;i<s->count;++i)
            if(i && xx_rt_memcmp(s->items[i].name+5,"audio-data.bin",14)==0 &&
               (uint64_t)s->items[i].size%fmt_align)return false;
    }
    s->size=(int64_t)total;return true;
}
void xx_audio_rifx_wave_init(xx_audio_rifx_wave*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AUDIO_RIFX_WAVE,"wav");}}
xx_audio_rifx_wave*xx_audio_rifx_wave_create(xx_io_device*d,int64_t b) {xx_audio_rifx_wave*r=(xx_audio_rifx_wave*)xx_mem_alloc(sizeof(*r));if(r)xx_audio_rifx_wave_init(r,d,b);return r;}
void xx_audio_rifx_wave_destroy(xx_audio_rifx_wave*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_audio_rifx_wave_free(xx_audio_rifx_wave*r) {if(r){xx_audio_rifx_wave_destroy(r);xx_mem_free(r);}}
bool xx_audio_rifx_wave_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_audio_rifx_wave_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
