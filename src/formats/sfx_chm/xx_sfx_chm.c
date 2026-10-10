/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/documents/xchmarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_chm/xx_sfx_chm.h"
#include "../common/xx_archive_carrier_readers.h"

static bool archive_carrier_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {static const uint8_t sig[]={0x49,0x54,0x53,0x46};return archive_carrier_carried(f,s,sig,sizeof(sig),0,12,archive_carrier_chm,"payload.chm",pd);}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return archive_carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_sfx_chm_init(xx_sfx_chm *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_CHM,"exe"); } }
xx_sfx_chm *xx_sfx_chm_create(xx_io_device *d,int64_t b) { xx_sfx_chm *r=(xx_sfx_chm *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_chm_init(r,d,b); return r; }
void xx_sfx_chm_destroy(xx_sfx_chm *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_chm_free(xx_sfx_chm *r) { if(r) { xx_sfx_chm_destroy(r); xx_mem_free(r); } }
bool xx_sfx_chm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_chm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
