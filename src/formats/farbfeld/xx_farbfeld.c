/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://git.suckless.org/farbfeld/file/FORMAT.html
 * Stored encoded component extraction; no media decoding claims.
 */
#include "xxfclib/formats/farbfeld/xx_farbfeld.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
    uint8_t h[16]; uint32_t w,height; uint64_t row,bytes; (void)pd;
    if(!pm_read(f,0,h,16) || xx_rt_memcmp(h,"farbfeld",8) || !(w=xx_data_get_u32(h+8, 4, 0, true)) || !(height=xx_data_get_u32(h+12, 4, 0, true))) return false;
    row=(uint64_t)w*8; if(row>(uint64_t)INT64_MAX/height) return false; bytes=row*height;
    if(bytes>(uint64_t)(pm_available(f)-16) || !pm_add(f,s,"dimensions.bin",8,8) || !pm_add(f,s,"rgba16be.bin",16,(int64_t)bytes)) return false;
    s->size=16+(int64_t)bytes; return true;
}

void xx_farbfeld_init(xx_farbfeld *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_FARBFELD,"farbfeld"); } }
xx_farbfeld *xx_farbfeld_create(xx_io_device *d,int64_t b) { xx_farbfeld *r=(xx_farbfeld *)xx_mem_alloc(sizeof(*r)); if(r) xx_farbfeld_init(r,d,b); return r; }
void xx_farbfeld_destroy(xx_farbfeld *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_farbfeld_free(xx_farbfeld *r) { if(r) { xx_farbfeld_destroy(r); xx_mem_free(r); } }
bool xx_farbfeld_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_farbfeld_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
