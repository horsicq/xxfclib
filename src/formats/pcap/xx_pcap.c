/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://www.ietf.org/archive/id/draft-ietf-opsawg-pcap-05.html
 * PCAP 2.4 packet bytes; both endians and micro/nanosecond timestamps.
 */
#include "xxfclib/formats/pcap/xx_pcap.h"
#include "../xx_payload_members.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[24],record[16]; bool be,nano; uint32_t magic,snap; int64_t at=24,left=pm_available(f);
    if(!pm_read(f,0,h,24)) return false;
    magic=pm_be32(h); be=magic==0xa1b2c3d4U || magic==0xa1b23c4dU;
    nano=magic==0xa1b23c4dU || magic==0x4d3cb2a1U;
    if(!be && magic!=0xd4c3b2a1U && magic!=0x4d3cb2a1U) return false;
    if((be?pm_be16(h+4):pm_le16(h+4))!=2 || (be?pm_be16(h+6):pm_le16(h+6))!=4) return false;
    snap=be?pm_be32(h+16):pm_le32(h+16); if(!snap) return false;
    if((be?pm_be32(h+20):pm_le32(h+20))&0x0bff0000U) return false;
    while(at<left) {
        uint32_t cap,original,fraction; char name[48];
        if((pd && xx_pd_is_stopped(pd)) || !pm_read(f,at,record,16)) return false;
        fraction=be?pm_be32(record+4):pm_le32(record+4);
        cap=be?pm_be32(record+8):pm_le32(record+8); original=be?pm_be32(record+12):pm_le32(record+12);
        if(fraction>=(nano?1000000000U:1000000U) || cap>snap || cap>original || cap>(uint64_t)(left-at-16)) return false;
        xx_rt_snprintf(name,sizeof(name),"packet-%u.bin",(unsigned)s->count);
        if(!pm_add(f,s,name,at+16,cap)) return false;
        at+=16+(int64_t)cap;
    }
    s->size=at; return s->count!=0;
}

void xx_pcap_init(xx_pcap *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_PCAP,"pcap"); } }
xx_pcap *xx_pcap_create(xx_io_device *d,int64_t b) { xx_pcap *r=(xx_pcap *)xx_mem_alloc(sizeof(*r)); if(r) xx_pcap_init(r,d,b); return r; }
void xx_pcap_destroy(xx_pcap *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_pcap_free(xx_pcap *r) { if(r) { xx_pcap_destroy(r); xx_mem_free(r); } }
bool xx_pcap_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_pcap_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
