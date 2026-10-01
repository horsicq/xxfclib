/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake-2/master/qcommon/qfiles.h
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/idtech_bsp/xx_idtech_bsp.h"
#include "../bethesda_bsa/xx_game_table.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[8],r[8]; uint32_t ver,count,i; uint64_t floor; int64_t total=pm_available(f);
    if(!gm_read(f,total,0,h,8) || xx_rt_memcmp(h,"IBSP",4)) return false;
    ver=pm_le32(h+4); if(ver==38) count=19; else if(ver==46) count=17; else return false;
    floor=8+(uint64_t)count*8; if(!gm_range(total,0,floor)) return false; s->size=(int64_t)floor;
    for(i=0;i<count;++i) { uint32_t at,n; char label[32];
        if(gm_stopped(pd) || !gm_read(f,total,8+(uint64_t)i*8,r,8)) return false;
        at=pm_le32(r); n=pm_le32(r+4); if(!n) continue;
        xx_rt_snprintf(label,sizeof(label),"lump-%02u.bin",i);
        if(!gm_add(f,s,label,at,n,floor,total)) return false;
    }
    return s->count!=0;
}
void xx_idtech_bsp_init(xx_idtech_bsp *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_IDTECH_BSP,"bin"); } }
xx_idtech_bsp *xx_idtech_bsp_create(xx_io_device *d,int64_t b) { xx_idtech_bsp *r=(xx_idtech_bsp *)xx_mem_alloc(sizeof(*r)); if(r) xx_idtech_bsp_init(r,d,b); return r; }
void xx_idtech_bsp_destroy(xx_idtech_bsp *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_idtech_bsp_free(xx_idtech_bsp *r) { if(r) { xx_idtech_bsp_destroy(r); xx_mem_free(r); } }
bool xx_idtech_bsp_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_idtech_bsp_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
