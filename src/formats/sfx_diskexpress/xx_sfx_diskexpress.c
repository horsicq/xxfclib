/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_diskexpress/xx_sfx_diskexpress.h"
#include "../common/xx_executable_carrier.h"

#include "xxfclib/formats/diskexpress/xx_diskexpress.h"
static bool executable_carrier_at_parse(Abstractformat *f,pm_stream *s,int64_t at,xx_pd_struct *pd) {
    xx_diskexpress *r; bool ok; int64_t size; 
    if(carrier_stop(pd)) { return false; } r=xx_diskexpress_create(f->device,f->base_address+at); if(!r) return false;
    ok=xx_format_handle_base_info(&r->format,pd);size=r->format.format_size;
    ok=ok && !carrier_stop(pd) && r->format.number_of_archive_records>0 && r->format.number_of_archive_records<=4096 && carrier_range(pm_available(f),at,(uint64_t)size);xx_diskexpress_free(r);
    return ok && executable_carrier_component(f,s,at,size,"payload.dxp");
}
static bool sfx_carrier_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={65,83};return executable_carrier_scan(f,s,sig,sizeof(sig),0,false,false,executable_carrier_at_parse,pd); }



static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return sfx_carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_sfx_diskexpress_init(xx_sfx_diskexpress *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_DISKEXPRESS,"exe"); } }
xx_sfx_diskexpress *xx_sfx_diskexpress_create(xx_io_device *d,int64_t b) { xx_sfx_diskexpress *r=(xx_sfx_diskexpress *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_diskexpress_init(r,d,b); return r; }
void xx_sfx_diskexpress_destroy(xx_sfx_diskexpress *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_diskexpress_free(xx_sfx_diskexpress *r) { if(r) { xx_sfx_diskexpress_destroy(r); xx_mem_free(r); } }
bool xx_sfx_diskexpress_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_diskexpress_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
