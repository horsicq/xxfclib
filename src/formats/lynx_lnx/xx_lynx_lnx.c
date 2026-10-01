/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/libretro/libretro-handy/blob/master/lynx/cart.cpp
 * Independently implemented bounded parser; input ownership remains with caller.
 */
#include "xxfclib/formats/lynx_lnx/xx_lynx_lnx.h"
#include "../xx_payload_members.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    (void)pd;

    uint8_t h[64]; uint16_t a,b; int64_t at=64;
    if(!pm_read(f,0,h,sizeof(h)) || xx_rt_memcmp(h,"LYNX",4) || pm_le16(h+8)!=1 || h[58]>2) return false;
    a=pm_le16(h+4); b=pm_le16(h+6);
    if((a!=256 && a!=512 && a!=1024 && a!=2048) || (b!=0 && b!=256 && b!=512 && b!=1024 && b!=2048)) return false;
    /* AUDIN doubles the banks in newer headers; support ordinary banks only. */
    if(h[59]&1) return false;
    if(!pm_add(f,s,"bank0.bin",at,(int64_t)a*256)) return false; at+=(int64_t)a*256;
    if(b) { if(!pm_add(f,s,"bank1.bin",at,(int64_t)b*256)) return false; at+=(int64_t)b*256; }
    s->size=at; return true;

}
void xx_lynx_lnx_init(xx_lynx_lnx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_LYNX_LNX,"lnx"); } }
xx_lynx_lnx *xx_lynx_lnx_create(xx_io_device *d,int64_t b) { xx_lynx_lnx *r=(xx_lynx_lnx *)xx_mem_alloc(sizeof(*r)); if(r) xx_lynx_lnx_init(r,d,b); return r; }
void xx_lynx_lnx_destroy(xx_lynx_lnx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_lynx_lnx_free(xx_lynx_lnx *r) { if(r) { xx_lynx_lnx_destroy(r); xx_mem_free(r); } }
bool xx_lynx_lnx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_lynx_lnx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
