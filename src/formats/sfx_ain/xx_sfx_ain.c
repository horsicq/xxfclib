/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xainarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_ain/xx_sfx_ain.h"
#include "../sfx_imp/xx_seventh_wrapper_table.h"

static bool w7_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {static const uint8_t sig[]={0x21};return w7_carried(f,s,sig,sizeof(sig),0,5,w7_ain,"payload.ain",pd);}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w7_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_ain_init(xx_sfx_ain *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_AIN,"exe"); } }
xx_sfx_ain *xx_sfx_ain_create(xx_io_device *d,int64_t b) { xx_sfx_ain *r=(xx_sfx_ain *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_ain_init(r,d,b); return r; }
void xx_sfx_ain_destroy(xx_sfx_ain *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_ain_free(xx_sfx_ain *r) { if(r) { xx_sfx_ain_destroy(r); xx_mem_free(r); } }
bool xx_sfx_ain_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_ain_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
