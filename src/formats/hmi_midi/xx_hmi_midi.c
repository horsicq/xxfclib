/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/Mindwerks/wildmidi/blob/master/src/f_hmi.c
 * Exports original HMI song header and track regions, not converted MIDI.
 */
#include "xxfclib/formats/hmi_midi/xx_hmi_midi.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
#ifndef HMI_MIDI
#define XX_FILE_TYPE_HMI_MIDI ((xx_file_type_t)1540)
#endif

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[370],entry[4],th[0x5b];
    uint32_t offsets[255],count,i,header_length;
    int64_t available=pm_available(f),table_end;
    char name[32];
    if(available<375 || !pm_read(f,0,h,sizeof(h)) ||
       xx_rt_memcmp(h,"HMI-MIDISONG061595",18) || !h[212] || !(count=h[228]))
        return false;
    table_end=370+(int64_t)count*4;
    if(table_end>available || (int64_t)count*17+370>available) return false;
    for(i=0;i<count;++i) {
        if((pd && xx_pd_is_stopped(pd)) ||
           !pm_read(f,370+(int64_t)i*4,entry,sizeof(entry))) return false;
        offsets[i]=xx_data_get_u32(entry, 4, 0, false);
        if((int64_t)offsets[i]<table_end ||
           (i && offsets[i]<=offsets[i-1]) ||
           (int64_t)offsets[i]>available-(int64_t)sizeof(th) ||
           !pm_read(f,offsets[i],th,sizeof(th)) ||
           xx_rt_memcmp(th,"HMI-MIDITRACK",13)) return false;
        header_length=xx_data_get_u32(th+0x57, 4, 0, false);
        if(header_length<sizeof(th) ||
           (uint64_t)header_length>=(uint64_t)(available-offsets[i])) return false;
    }
    if(!pm_add(f,s,"song-header.bin",0,offsets[0])) return false;
    for(i=0;i<count;++i) {
        int64_t end=i+1<count ? offsets[i+1] : available;
        if(end-offsets[i]<(int64_t)sizeof(th)) return false;
        if(!pm_read(f,offsets[i]+0x57,entry,4) ||
           xx_data_get_u32(entry, 4, 0, false)>=(uint64_t)(end-offsets[i])) return false;
        xx_rt_snprintf(name,sizeof(name),"track-%03u.hmi",(unsigned)i);
        if(!pm_add(f,s,name,offsets[i],end-offsets[i])) return false;
    }
    s->size=available; return true;
}

void xx_hmi_midi_init(xx_hmi_midi *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_HMI_MIDI,"hmi"); } }
xx_hmi_midi *xx_hmi_midi_create(xx_io_device *d,int64_t b) { xx_hmi_midi *r=(xx_hmi_midi *)xx_mem_alloc(sizeof(*r)); if(r) xx_hmi_midi_init(r,d,b); return r; }
void xx_hmi_midi_destroy(xx_hmi_midi *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_hmi_midi_free(xx_hmi_midi *r) { if(r) { xx_hmi_midi_destroy(r); xx_mem_free(r); } }
bool xx_hmi_midi_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_hmi_midi_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
