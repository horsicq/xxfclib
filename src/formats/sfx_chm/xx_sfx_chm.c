/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/documents/xchmarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_chm/xx_sfx_chm.h"
#include "../sfx_imp/xx_seventh_wrapper_table.h"

static bool w7_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {static const uint8_t sig[]={0x49,0x54,0x53,0x46};return w7_carried(f,s,sig,sizeof(sig),0,12,w7_chm,"payload.chm",pd);}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return w7_parse(f,s,pd) && wg_members(s,pd); }
void xx_sfx_chm_init(xx_sfx_chm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_CHM,"exe"); } }
xx_sfx_chm *xx_sfx_chm_create(xx_io_device *d,int64_t b) { xx_sfx_chm *r=(xx_sfx_chm *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_chm_init(r,d,b); return r; }
void xx_sfx_chm_destroy(xx_sfx_chm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_chm_free(xx_sfx_chm *r) { if(r) { xx_sfx_chm_destroy(r); xx_mem_free(r); } }
bool xx_sfx_chm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_chm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
