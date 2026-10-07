/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ValveSoftware/source-sdk-2013/master/src/public/bspfile.h
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#include "xxfclib/formats/valve_bsp/xx_valve_bsp.h"
#include "../bethesda_bsa/xx_game_table.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

    uint8_t h[8],r[16]; uint32_t ver,i; uint64_t floor=1036; int64_t total=pm_available(f);
    if(!gm_read(f,total,0,h,8) || xx_rt_memcmp(h,"VBSP",4) || !gm_range(total,0,floor)) return false;
    ver=xx_data_get_u32(h+4, 4, 0, false); if(ver<19 || ver>21) return false; s->size=(int64_t)floor;
    for(i=0;i<64;++i) { uint32_t at,n; char label[32];
        if(gm_stopped(pd) || !gm_read(f,total,8+(uint64_t)i*16,r,16)) return false;
        at=xx_data_get_u32(r, 4, 0, false); n=xx_data_get_u32(r+4, 4, 0, false); if(xx_data_get_u32(r+12, 4, 0, false)!=0) return false; if(!n) continue;
        xx_rt_snprintf(label,sizeof(label),i==40 ? "lump-%02u.zip" : "lump-%02u.bin",i);
        if(!gm_add(f,s,label,at,n,floor,total)) return false;
    }
    return s->count!=0;
}
void xx_valve_bsp_init(xx_valve_bsp *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_VALVE_BSP,"bin"); } }
xx_valve_bsp *xx_valve_bsp_create(xx_io_device *d,int64_t b) { xx_valve_bsp *r=(xx_valve_bsp *)xx_mem_alloc(sizeof(*r)); if(r) xx_valve_bsp_init(r,d,b); return r; }
void xx_valve_bsp_destroy(xx_valve_bsp *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_valve_bsp_free(xx_valve_bsp *r) { if(r) { xx_valve_bsp_destroy(r); xx_mem_free(r); } }
bool xx_valve_bsp_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_valve_bsp_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
