/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_zpak/xx_sfx_zpak.h"
#include "../sfx_arcv2/xx_sixth_wrapper_table.h"

#include "xxfclib/formats/zpak/xx_zpak.h"
static bool w6_at_parse(Abstractformat *f,pm_stream *s,int64_t at,xx_pd_struct *pd) {
    xx_zpak *r; bool ok; int64_t size; 
    if(wg_stop(pd)) { return false; } r=xx_zpak_create(f->device,f->base_address+at); if(!r) return false;
    ok=xx_format_handle_base_info(&r->format,pd);size=r->format.format_size;
    ok=ok && !wg_stop(pd) && r->format.number_of_archive_records>0 && r->format.number_of_archive_records<=4096 && wg_range(pm_available(f),at,(uint64_t)size);xx_zpak_free(r);
    return ok && w6_component(f,s,at,size,"payload.zpak");
}
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={122,112,97,107};return w6_scan(f,s,sig,sizeof(sig),0,false,false,w6_at_parse,pd); }



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_zpak_init(xx_sfx_zpak *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_ZPAK,"exe"); } }
xx_sfx_zpak *xx_sfx_zpak_create(xx_io_device *d,int64_t b) { xx_sfx_zpak *r=(xx_sfx_zpak *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_zpak_init(r,d,b); return r; }
void xx_sfx_zpak_destroy(xx_sfx_zpak *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_zpak_free(xx_sfx_zpak *r) { if(r) { xx_sfx_zpak_destroy(r); xx_mem_free(r); } }
bool xx_sfx_zpak_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_zpak_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
