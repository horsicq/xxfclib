/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://github.com/wireshark/wireshark/blob/master/wiretap/btsnoop.c
 * BTSnoop v1 raw packet records; no protocol decoding.
 */
#include "xxfclib/formats/btsnoop/xx_btsnoop.h"
#include "../xx_payload_members.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[24]; int64_t at=16,left=pm_available(f); uint32_t link;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"btsnoop\0",8) || pm_be32(h+8)!=1) return false;
    link=pm_be32(h+12); if(link!=1001 && link!=1002 && link!=1003 && link!=1004 && link!=2001) return false;
    while(at<left) {
        uint32_t original,cap; char name[48];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,h,24)) return false;
        original=pm_be32(h); cap=pm_be32(h+4);
        if(cap>original || cap>(uint64_t)(left-at-24)) return false;
        xx_rt_snprintf(name,sizeof(name),"packet-%u.bin",(unsigned)s->count);
        if(!pm_add(f,s,name,at+24,cap)) return false;
        at+=24+(int64_t)cap;
    }
    s->size=at; return s->count!=0;
}

void xx_btsnoop_init(xx_btsnoop *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_BTSNOOP,"btsnoop"); } }
xx_btsnoop *xx_btsnoop_create(xx_io_device *d,int64_t b) { xx_btsnoop *r=(xx_btsnoop *)xx_mem_alloc(sizeof(*r)); if(r) xx_btsnoop_init(r,d,b); return r; }
void xx_btsnoop_destroy(xx_btsnoop *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_btsnoop_free(xx_btsnoop *r) { if(r) { xx_btsnoop_destroy(r); xx_mem_free(r); } }
bool xx_btsnoop_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_btsnoop_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
