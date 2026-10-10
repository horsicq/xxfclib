/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xalzarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_alz/xx_sfx_alz.h"
#include "../common/xx_archive_carrier_readers.h"

static bool archive_carrier_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {static const uint8_t sig[]={0x41,0x4c,0x5a,0x01};return archive_carrier_carried(f,s,sig,sizeof(sig),0,11,archive_carrier_alz,"payload.alz",pd);}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return archive_carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_sfx_alz_init(xx_sfx_alz *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_ALZ,"exe"); } }
xx_sfx_alz *xx_sfx_alz_create(xx_io_device *d,int64_t b) { xx_sfx_alz *r=(xx_sfx_alz *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_alz_init(r,d,b); return r; }
void xx_sfx_alz_destroy(xx_sfx_alz *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_alz_free(xx_sfx_alz *r) { if(r) { xx_sfx_alz_destroy(r); xx_mem_free(r); } }
bool xx_sfx_alz_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_alz_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
