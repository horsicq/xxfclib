/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_arc/xx_sfx_arc.h"
#include "../common/xx_sfx_carrier.h"

#include "xxfclib/formats/seaarc/xx_seaarc.h"
static Abstractformat *nested_open(xx_io_device *d,int64_t at) { xx_seaarc *r=xx_seaarc_create(d,at); return r ? &r->format : NULL; }
static void nested_close(Abstractformat *f) { xx_seaarc_free((xx_seaarc *)f); }
static bool sfx_carrier_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={0x1a}; int64_t low;
    if(!sfx_carrier_carrier(f,false,&low,pd)) return false; 
    return sfx_carrier_embedded(f,s,low,sig,sizeof(sig),0,nested_open,nested_close,"payload.arc",pd);
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return sfx_carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_sfx_arc_init(xx_sfx_arc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_ARC,"exe"); } }
xx_sfx_arc *xx_sfx_arc_create(xx_io_device *d,int64_t b) { xx_sfx_arc *r=(xx_sfx_arc *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_arc_init(r,d,b); return r; }
void xx_sfx_arc_destroy(xx_sfx_arc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_arc_free(xx_sfx_arc *r) { if(r) { xx_sfx_arc_destroy(r); xx_mem_free(r); } }
bool xx_sfx_arc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_arc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
