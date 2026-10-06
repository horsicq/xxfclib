/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xarcv2sfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_arcv2/xx_sfx_arcv2.h"
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"

static bool w6_arc_at(Abstractformat *f,pm_stream *s,int64_t base,xx_pd_struct *pd) {
    int64_t at=base,limit=pm_available(f);unsigned archives=0,members=0;uint64_t checks=0;uint8_t h[300];
    while(at<limit) { int64_t start=at;unsigned count=0;char label[48];
        if(wg_stop(pd) || ++archives>64 || !pm_read(f,at,h,14) || xx_rt_memcmp(h,"ARCV\0\2\x0e\0",8) || (pm_le32(h+8)!=1 && pm_le32(h+8)!=2 && pm_le32(h+8)!=4 && pm_le32(h+8)!=8)) { return false; } at+=14;
        while(limit-at>=4 && pm_read(f,at,h,4) && !xx_rt_memcmp(h,"BLCK",4)) { unsigned name;uint32_t flags,n,raw,packed,crc;const uint8_t *tail;int64_t data;
            if(wg_stop(pd) || ++members>4096 || !pm_read(f,at,h,17) || pm_le16(h+4)!=512 || !(name=h[16]) || pm_le16(h+6)!=45+name || !pm_read(f,at,h,45+name)) return false;
            flags=pm_le32(h+8);n=pm_le32(h+12);if(flags!=0x11 && flags!=0x21) return false;
            { unsigned j;for(j=0;j<name;++j) if(h[17+j]<32 || h[17+j]==127) return false; }
            tail=h+17+name;raw=pm_le32(tail);packed=pm_le32(tail+4);crc=pm_le32(tail+24);data=at+45+name;
            if(n!=packed || !wg_range(limit,data,n) || (flags==0x11 && raw!=n)) return false;
            if(flags==0x11) { if(n>67108864-checks || !w5_crc(f,data,n,crc,pd)) return false;checks+=n; }
            at=data+n;++count;
        }
        if(!count) { return false; } xx_rt_snprintf(label,sizeof(label),"archive-%u.arv",archives-1);if(!pm_add(f,s,label,start,at-start)) return false;
    } s->size=limit;return members!=0;
}
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={'A','R','C','V',0,2,14,0};return w6_first(f,s,sig,sizeof(sig),0,false,false,w6_arc_at,pd); }



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_arcv2_init(xx_sfx_arcv2 *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_ARCV2,"exe"); } }
xx_sfx_arcv2 *xx_sfx_arcv2_create(xx_io_device *d,int64_t b) { xx_sfx_arcv2 *r=(xx_sfx_arcv2 *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_arcv2_init(r,d,b); return r; }
void xx_sfx_arcv2_destroy(xx_sfx_arcv2 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_arcv2_free(xx_sfx_arcv2 *r) { if(r) { xx_sfx_arcv2_destroy(r); xx_mem_free(r); } }
bool xx_sfx_arcv2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_arcv2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
