/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/vgmstream/vgmstream/blob/master/src/meta/strm_abylight.c
 * Extracts the original 30-byte wrapper and complete ADTS AAC byte stream.
 */
#include "xxfclib/formats/abylight_strm/xx_abylight_strm.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
#ifndef ABYLIGHT_STRM
#define XX_FILE_TYPE_ABYLIGHT_STRM ((xx_file_type_t)1542)
#endif

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    static const uint32_t rates[16]={96000,88200,64000,48000,44100,32000,
        24000,22050,16000,12000,11025,8000,7350,0,0,0};
    uint8_t h[30],adts[7]; uint32_t rate,length,position=0,frames=0;
    int64_t available=pm_available(f);
    if(available<37 || !pm_read(f,0,h,sizeof(h)) ||
       xx_rt_memcmp(h,"STRM",4) || xx_data_get_u32(h+4, 4, 0, false)!=1000 ||
       !(rate=xx_data_get_u32(h+8, 4, 0, false)) || !(length=xx_data_get_u32(h+0x10, 4, 0, false)) ||
       length!=xx_data_get_u32(h+0x18, 4, 0, false) || (uint64_t)length!=(uint64_t)(available-30))
        return false;
    while(position<length) {
        uint32_t frame_length,frequency,channels,header_length;
        if((pd && xx_pd_is_stopped(pd)) || ++frames>1000000U ||
           length-position<7 || !pm_read(f,30+(int64_t)position,adts,sizeof(adts)))
            return false;
        if(adts[0]!=0xff || (adts[1]&0xf6)!=0xf0 ||
           (adts[2]&0xc0)==0xc0) return false;
        frequency=(adts[2]>>2)&15U;
        channels=((uint32_t)(adts[2]&1U)<<2)|(adts[3]>>6);
        if(rates[frequency]!=rate || channels!=2U) return false;
        frame_length=((uint32_t)(adts[3]&3U)<<11)|((uint32_t)adts[4]<<3)|(adts[5]>>5);
        header_length=(adts[1]&1U)?7U:9U;
        if(frame_length<header_length || frame_length>length-position) return false;
        position+=frame_length;
    }
    if(!frames || !pm_add(f,s,"header.bin",0,30) ||
       !pm_add(f,s,"audio.aac",30,length)) return false;
    s->size=available; return true;
}

void xx_abylight_strm_init(xx_abylight_strm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_ABYLIGHT_STRM,"strm"); } }
xx_abylight_strm *xx_abylight_strm_create(xx_io_device *d,int64_t b) { xx_abylight_strm *r=(xx_abylight_strm *)xx_mem_alloc(sizeof(*r)); if(r) xx_abylight_strm_init(r,d,b); return r; }
void xx_abylight_strm_destroy(xx_abylight_strm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_abylight_strm_free(xx_abylight_strm *r) { if(r) { xx_abylight_strm_destroy(r); xx_mem_free(r); } }
bool xx_abylight_strm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_abylight_strm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
