/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent stored-packet reader. Layout and 0x80 channel interleave:
 * https://github.com/vgmstream/vgmstream/blob/master/src/meta/riff_ima.c
 */
#include "xxfclib/formats/audio_riff_ima/xx_audio_riff_ima.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

#ifndef XX_FILE_TYPE_AUDIO_RIFF_IMA
#define XX_FILE_TYPE_AUDIO_RIFF_IMA ((xx_file_type_t)1523)
#endif

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd)
{
    uint8_t h[0x2c];
    int64_t available=pm_available(f);
    uint32_t channels,rate;
    uint64_t data_size,packets,group_packets,p=0;
    char label[64];
    if (available<0x2c+0x80 || !pm_read(f,0,h,sizeof(h)) ||
        xx_rt_memcmp(h,"RIFF",4) || xx_rt_memcmp(h+8,"IMA ",4) ||
        xx_data_get_u32(h+4, 4, 0, false)!=(uint64_t)available) return false;
    rate=xx_data_get_u32(h+0x0c, 4, 0, false); channels=xx_data_get_u32(h+0x24, 4, 0, false);
    data_size=(uint64_t)available-0x2cU;
    packets=(data_size+0x7fU)/0x80U;
    if (rate<4000 || rate>192000 || !channels || channels>8 ||
        packets<channels ||
        xx_data_get_u32(h+0x20, 4, 0, false)>xx_data_get_u32(h+0x28, 4, 0, false)) return false;
    group_packets=(packets+8190U)/8191U;
    if (!pm_add(f,s,"header.bin",0,0x2c)) return false;
    while (p<packets) {
        uint64_t first_byte=p*0x80U;
        uint64_t span=group_packets*0x80U;
        if (span>data_size-first_byte) span=data_size-first_byte;
        if (pd && xx_pd_is_stopped(pd)) return false;
        if (group_packets==1U)
            (void)xx_rt_snprintf(label,sizeof(label),"packet-%05llu-channel-%02u.ima",
                                 (unsigned long long)(p/channels),(unsigned)(p%channels));
        else
            (void)xx_rt_snprintf(label,sizeof(label),"packets-%05llu-through-%05llu.ima",
                                 (unsigned long long)p,
                                 (unsigned long long)((p+group_packets<packets ? p+group_packets:packets)-1U));
        if (!pm_add(f,s,label,0x2c+(int64_t)first_byte,(int64_t)span)) return false;
        p+=group_packets;
    }
    s->size=available;
    return true;
}

void xx_audio_riff_ima_init(xx_audio_riff_ima *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_AUDIO_RIFF_IMA,"strm"); } }
xx_audio_riff_ima *xx_audio_riff_ima_create(xx_io_device *d,int64_t b) { xx_audio_riff_ima *r=(xx_audio_riff_ima *)xx_mem_alloc(sizeof(*r)); if(r) xx_audio_riff_ima_init(r,d,b); return r; }
void xx_audio_riff_ima_destroy(xx_audio_riff_ima *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_audio_riff_ima_free(xx_audio_riff_ima *r) { if(r) { xx_audio_riff_ima_destroy(r); xx_mem_free(r); } }
bool xx_audio_riff_ima_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_audio_riff_ima_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
