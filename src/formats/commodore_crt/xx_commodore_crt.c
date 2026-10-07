/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://vice-emu.sourceforge.io/vice_17.html
 * Independently implemented bounded parser; input ownership remains with caller.
 */
#include "xxfclib/formats/commodore_crt/xx_commodore_crt.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    (void)pd;

    uint8_t h[64],c[16]; int64_t at,total=pm_available(f); uint32_t head;
    if(!pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"C64 CARTRIDGE   ",16)) return false;
    head=xx_data_get_u32(h+16, 4, 0, true);
    if(head<64 || head>4096 || head>(uint64_t)total || (xx_data_get_u16(h+20, 2, 0, true)!=0x100 && xx_data_get_u16(h+20, 2, 0, true)!=0x101) || h[24]>1 || h[25]>1) return false;
    at=head;
    while(at<total) {
        uint32_t packet; uint16_t bytes; char name[64];
        if(pd && xx_pd_is_stopped(pd)) return false;
        if(!pm_read(f,at,c,16) || xx_rt_memcmp(c,"CHIP",4)) return false;
        packet=xx_data_get_u32(c+4, 4, 0, true); bytes=xx_data_get_u16(c+14, 2, 0, true);
        if(bytes==0 || packet!=(uint32_t)bytes+16 || xx_data_get_u16(c+8, 2, 0, true)>2) return false;
        xx_rt_snprintf(name,sizeof(name),"bank-%u-at-%04x.bin",(unsigned)xx_data_get_u16(c+10, 2, 0, true),(unsigned)xx_data_get_u16(c+12, 2, 0, true));
        if(!pm_add(f,s,name,at+16,bytes)) { return false; } at+=packet;
    }
    s->size=at; return s->count!=0;

}
void xx_commodore_crt_init(xx_commodore_crt *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_COMMODORE_CRT,"crt"); } }
xx_commodore_crt *xx_commodore_crt_create(xx_io_device *d,int64_t b) { xx_commodore_crt *r=(xx_commodore_crt *)xx_mem_alloc(sizeof(*r)); if(r) xx_commodore_crt_init(r,d,b); return r; }
void xx_commodore_crt_destroy(xx_commodore_crt *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_commodore_crt_free(xx_commodore_crt *r) { if(r) { xx_commodore_crt_destroy(r); xx_mem_free(r); } }
bool xx_commodore_crt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_commodore_crt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
