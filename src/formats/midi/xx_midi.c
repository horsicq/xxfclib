/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://midi.org/standard-midi-files
 * Independently implemented bounded parser; input ownership remains with caller.
 */
#include "xxfclib/formats/midi/xx_midi.h"
#include "../xx_payload_members.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    (void)pd;

    uint8_t h[14],t[8]; uint32_t head,n,i; uint16_t kind,division; int64_t at;
    if(!pm_read(f,0,h,14) || xx_rt_memcmp(h,"MThd",4)) return false;
    head=pm_be32(h+4); kind=pm_be16(h+8); n=pm_be16(h+10); division=pm_be16(h+12);
    if(head<6 || head>1024 || kind>2 || n==0 || (kind==0 && n!=1) || division==0) return false;
    if(division&0x8000) {
        uint8_t fps=(uint8_t)(division>>8);
        if((fps!=0xe8 && fps!=0xe7 && fps!=0xe3 && fps!=0xe2) || (division&255)==0) return false;
    }
    at=8+head;
    for(i=0;i<n;++i) {
        uint32_t bytes; char name[40];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,t,8) || xx_rt_memcmp(t,"MTrk",4)) return false;
        bytes=pm_be32(t+4); if(bytes<4) return false;
        xx_rt_snprintf(name,sizeof(name),"track-%u.events",(unsigned)i);
        if(!pm_add(f,s,name,at+8,bytes)) { return false; } at+=8+(int64_t)bytes;
    }
    s->size=at; return true;

}
void xx_midi_init(xx_midi *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_MIDI,"mid"); } }
xx_midi *xx_midi_create(xx_io_device *d,int64_t b) { xx_midi *r=(xx_midi *)xx_mem_alloc(sizeof(*r)); if(r) xx_midi_init(r,d,b); return r; }
void xx_midi_destroy(xx_midi *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_midi_free(xx_midi *r) { if(r) { xx_midi_destroy(r); xx_mem_free(r); } }
bool xx_midi_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_midi_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
