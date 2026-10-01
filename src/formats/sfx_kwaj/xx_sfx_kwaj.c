/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_kwaj/xx_sfx_kwaj.h"
#include "../sfx_arc/xx_fifth_wrapper_table.h"

#include "xxfclib/formats/kwaj/xx_kwaj.h"
static Abstractformat *nested_open(xx_io_device *d,int64_t at) { xx_kwaj *r=xx_kwaj_create(d,at); return r ? &r->format : NULL; }
static void nested_close(Abstractformat *f) { xx_kwaj_free((xx_kwaj *)f); }
static bool w5_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { static const uint8_t sig[]={0x4b,0x57,0x41,0x4a,0x88,0xf0,0x27,0xd1}; int64_t low;
    if(!w5_carrier(f,false,&low,pd)) return false; low=64;
    return w5_embedded(f,s,low,sig,sizeof(sig),0,nested_open,nested_close,"payload.kwaj",pd);
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w5_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_kwaj_init(xx_sfx_kwaj *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_KWAJ,"exe"); } }
xx_sfx_kwaj *xx_sfx_kwaj_create(xx_io_device *d,int64_t b) { xx_sfx_kwaj *r=(xx_sfx_kwaj *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_kwaj_init(r,d,b); return r; }
void xx_sfx_kwaj_destroy(xx_sfx_kwaj *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_kwaj_free(xx_sfx_kwaj *r) { if(r) { xx_sfx_kwaj_destroy(r); xx_mem_free(r); } }
bool xx_sfx_kwaj_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_kwaj_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
