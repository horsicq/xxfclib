/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_arc/xx_sfx_arc.h"
#include "../sfx_arc/xx_fifth_wrapper_table.h"

#include "xxfclib/formats/seaarc/xx_seaarc.h"
static Abstractformat *nested_open(xx_io_device *d,int64_t at) { xx_seaarc *r=xx_seaarc_create(d,at); return r ? &r->format : NULL; }
static void nested_close(Abstractformat *f) { xx_seaarc_free((xx_seaarc *)f); }
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={0x1a}; int64_t low;
    if(!w5_carrier(f,false,&low,pd)) return false; 
    return w5_embedded(f,s,low,sig,sizeof(sig),0,nested_open,nested_close,"payload.arc",pd);
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_arc_init(xx_sfx_arc *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_ARC,"exe"); } }
xx_sfx_arc *xx_sfx_arc_create(xx_io_device *d,int64_t b) { xx_sfx_arc *r=(xx_sfx_arc *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_arc_init(r,d,b); return r; }
void xx_sfx_arc_destroy(xx_sfx_arc *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_arc_free(xx_sfx_arc *r) { if(r) { xx_sfx_arc_destroy(r); xx_mem_free(r); } }
bool xx_sfx_arc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_arc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
