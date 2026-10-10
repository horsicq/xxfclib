/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: src/formats/cazip/xx_cazip.c
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_cazip/xx_sfx_cazip.h"
#include "../common/xx_archive_carrier_readers.h"

static bool archive_carrier_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {static const uint8_t sig[]={0x0d,0x0a,0x1a,0x43,0x41,0x5a,0x49,0x50};return archive_carrier_carried(f,s,sig,sizeof(sig),0,8,archive_carrier_cazip,"payload.cazip",pd);}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return archive_carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_sfx_cazip_init(xx_sfx_cazip *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_CAZIP,"exe"); } }
xx_sfx_cazip *xx_sfx_cazip_create(xx_io_device *d,int64_t b) { xx_sfx_cazip *r=(xx_sfx_cazip *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_cazip_init(r,d,b); return r; }
void xx_sfx_cazip_destroy(xx_sfx_cazip *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_cazip_free(xx_sfx_cazip *r) { if(r) { xx_sfx_cazip_destroy(r); xx_mem_free(r); } }
bool xx_sfx_cazip_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_cazip_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
