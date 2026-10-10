/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/archives/xsqxarchive.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_sqx/xx_sfx_sqx.h"
#include "../common/xx_archive_carrier_readers.h"

static bool archive_carrier_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {static const uint8_t sig[]={0x2d,0x73,0x71,0x78,0x2d};return archive_carrier_carried(f,s,sig,sizeof(sig),-7,4,archive_carrier_sqx,"payload.sqx",pd);}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return archive_carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_sfx_sqx_init(xx_sfx_sqx *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_SQX,"exe"); } }
xx_sfx_sqx *xx_sfx_sqx_create(xx_io_device *d,int64_t b) { xx_sfx_sqx *r=(xx_sfx_sqx *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_sqx_init(r,d,b); return r; }
void xx_sfx_sqx_destroy(xx_sfx_sqx *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_sqx_free(xx_sfx_sqx *r) { if(r) { xx_sfx_sqx_destroy(r); xx_mem_free(r); } }
bool xx_sfx_sqx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_sqx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
