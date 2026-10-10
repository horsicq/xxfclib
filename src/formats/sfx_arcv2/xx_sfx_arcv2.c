/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xarcv2sfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_arcv2/xx_sfx_arcv2.h"
#include "../common/xx_executable_carrier.h"

static bool executable_carrier_arc_at(Abstractformat *f,pm_stream *s,int64_t base,xx_pd_struct *pd) {
    int64_t at=base,limit=pm_available(f);unsigned archives=0,members=0;uint64_t checks=0;uint8_t h[300];
    while(at<limit) { int64_t start=at;unsigned count=0;char label[48];
        if(carrier_stop(pd) || ++archives>64 || !pm_read(f,at,h,14) || xx_rt_memcmp(h,"ARCV\0\2\x0e\0",8) || (xx_data_get_u32(h+8, 4, 0, false)!=1 && xx_data_get_u32(h+8, 4, 0, false)!=2 && xx_data_get_u32(h+8, 4, 0, false)!=4 && xx_data_get_u32(h+8, 4, 0, false)!=8)) { return false; } at+=14;
        while(limit-at>=4 && pm_read(f,at,h,4) && !xx_rt_memcmp(h,"BLCK",4)) { unsigned name;uint32_t flags,n,raw,packed,crc;const uint8_t *tail;int64_t data;
            if(carrier_stop(pd) || ++members>4096 || !pm_read(f,at,h,17) || xx_data_get_u16(h+4, 2, 0, false)!=512 || !(name=h[16]) || xx_data_get_u16(h+6, 2, 0, false)!=45+name || !pm_read(f,at,h,45+name)) return false;
            flags=xx_data_get_u32(h+8, 4, 0, false);n=xx_data_get_u32(h+12, 4, 0, false);if(flags!=0x11 && flags!=0x21) return false;
            { unsigned j;for(j=0;j<name;++j) if(h[17+j]<32 || h[17+j]==127) return false; }
            tail=h+17+name;raw=xx_data_get_u32(tail, 4, 0, false);packed=xx_data_get_u32(tail+4, 4, 0, false);crc=xx_data_get_u32(tail+24, 4, 0, false);data=at+45+name;
            if(n!=packed || !carrier_range(limit,data,n) || (flags==0x11 && raw!=n)) return false;
            if(flags==0x11) { if(n>67108864-checks || !sfx_carrier_crc(f,data,n,crc,pd)) return false;checks+=n; }
            at=data+n;++count;
        }
        if(!count) { return false; } xx_rt_snprintf(label,sizeof(label),"archive-%u.arv",archives-1);if(!pm_add(f,s,label,start,at-start)) return false;
    } s->size=limit;return members!=0;
}
static bool sfx_carrier_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={'A','R','C','V',0,2,14,0};return executable_carrier_first(f,s,sig,sizeof(sig),0,false,false,executable_carrier_arc_at,pd); }



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return sfx_carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_sfx_arcv2_init(xx_sfx_arcv2 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_ARCV2,"exe"); } }
xx_sfx_arcv2 *xx_sfx_arcv2_create(xx_io_device *d,int64_t b) { xx_sfx_arcv2 *r=(xx_sfx_arcv2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_arcv2_init(r,d,b); return r; }
void xx_sfx_arcv2_destroy(xx_sfx_arcv2 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_arcv2_free(xx_sfx_arcv2 *r) { if(r) { xx_sfx_arcv2_destroy(r); xx_mem_free(r); } }
bool xx_sfx_arcv2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_arcv2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
