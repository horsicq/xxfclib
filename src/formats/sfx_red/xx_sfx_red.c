/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Bounded independent carrier/container parser. No payload execution.
 */
#include "xxfclib/formats/sfx_red/xx_sfx_red.h"
#include "../common/xx_archive_carrier_readers.h"

static bool archive_carrier_parse(Abstractformat*f,pm_stream*s,xx_pd_struct*pd) {static const uint8_t sig[]={0x52,0x52,0x01};return archive_carrier_carried(f,s,sig,sizeof(sig),0,1,archive_carrier_red,"payload.red",pd);}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { return archive_carrier_parse(f,s,pd) && carrier_members(s,pd); }
void xx_sfx_red_init(xx_sfx_red *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_SFX_RED,"exe"); } }
xx_sfx_red *xx_sfx_red_create(xx_io_device *d,int64_t b) { xx_sfx_red *r=(xx_sfx_red *)xx_mem_alloc(sizeof(*r)); if(r) xx_sfx_red_init(r,d,b); return r; }
void xx_sfx_red_destroy(xx_sfx_red *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sfx_red_free(xx_sfx_red *r) { if(r) { xx_sfx_red_destroy(r); xx_mem_free(r); } }
bool xx_sfx_red_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sfx_red_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
