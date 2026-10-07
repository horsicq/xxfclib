/* SPDX-License-Identifier: MIT. Original framed components, no playback or payload execution. */
#include "xxfclib/formats/audio_adx/xx_audio_adx.h"
#include "../tracker_liquid/xx_eighth_components.h"
#ifndef AUDIO_ADX
#define XX_FILE_TYPE_AUDIO_ADX ((xx_file_type_t)816)
#endif
static bool e8_parse(e8_blob*c) {
 size_t header,at,end;uint32_t samples,rate;unsigned channels,encoding,version;
 uint64_t expected,bytes;bool eof=false,encrypted;
 if(!e8_range(c,0,20) || xx_data_get_u16(c->b, 2, 0, true)!=0x8000 ||
    (header=4U+xx_data_get_u16(c->b+2, 2, 0, true))<26 || header>65535 ||
    !e8_range(c,0,header) || !e8_eq(c,header-6,"(c)CRI",6))return false;
 encoding=c->b[4];channels=c->b[7];rate=xx_data_get_u32(c->b+8, 4, 0, true);
 samples=xx_data_get_u32(c->b+12, 4, 0, true);version=xx_data_get_u16(c->b+18, 2, 0, true);
 encrypted=version==0x0408 || version==0x0409;
 if((encoding!=2 && encoding!=3 && encoding!=4) || c->b[5]!=18 ||
    c->b[6]!=4 || !channels || channels>8 || rate<4000 || rate>384000 ||
    !samples || samples>100000000 ||
    (version!=0x0300 && version!=0x0400 && version!=0x0408 &&
     version!=0x0409 && version!=0x0500) ||
    (version!=0x0300 && header<30) ||
    (encoding==3 && !encrypted && xx_data_get_u16(c->b+16, 2, 0, true)>=rate/2U))return false;
 end=c->n;
 if(end>=header+18 && xx_data_get_u16(c->b+end-18, 2, 0, true)==0x8001 &&
    xx_data_get_u16(c->b+end-16, 2, 0, true)==14) {
    if(!e8_zero(c,end-14,14))return false;
    end-=18;eof=true;
 }
 bytes=end-header;
 expected=((uint64_t)samples+31U)/32U*18U*channels;
 if(!bytes || bytes%(18U*channels) ||
    (bytes!=expected && (!eof || channels!=1 || expected!=bytes+18U)))
    return false;
 /* The first word is a scale for type 3, but types 2/4 use different
    coefficient/scale bits. Encryption also transforms that word. */
 if(encoding==3 && !encrypted)
    for(at=header;at<end;at+=18)
        if(xx_data_get_u16(c->b+at, 2, 0, true)&0x8000)return false;
 if(!e8_add(c,"header.bin",0,header) ||
    !e8_add(c,"adpcm-frames.bin",header,(size_t)bytes))return false;
 return !eof || e8_add(c,"terminator.bin",end,18);
}
static bool pm_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {return e8_loaded(f,s,pd,e8_parse);}
void xx_audio_adx_init(xx_audio_adx*r,xx_io_device*d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AUDIO_ADX,"bin");}}
xx_audio_adx*xx_audio_adx_create(xx_io_device*d,int64_t b) {xx_audio_adx*r=(xx_audio_adx*)xx_mem_alloc(sizeof(*r));if(r)xx_audio_adx_init(r,d,b);return r;}
void xx_audio_adx_destroy(xx_audio_adx*r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_audio_adx_free(xx_audio_adx*r) {if(r){xx_audio_adx_destroy(r);xx_mem_free(r);}}
bool xx_audio_adx_check_is_valid(Abstractformat*f,xx_pd_struct*pd) {return pm_valid(f,pd);}
bool xx_audio_adx_handle_base_info(Abstractformat*f,xx_pd_struct*pd) {return pm_handle(f,pd);}
