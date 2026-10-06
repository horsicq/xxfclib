/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
/* Layout: https://vice-emu.sourceforge.io/vice_17.html
 * C64 TAP versions 0/1; validates full pulse framing and exports descriptor and original pulse train. No cassette demodulation.
 */
#include "xxfclib/formats/commodore_tap/xx_commodore_tap.h"
#include "../vice_x64/xx_ninth_retro.h"

static bool parse_blob(Abstractformat *f,pm_stream *s,nh_blob *b) {
 uint32_t at=20,end;
 if(!nh_range(b,0,20) || xx_rt_memcmp(b->p,"C64-TAPE-RAW",12) || b->p[12]>1 || b->p[13] || b->p[14]>3 || b->p[15] || !pm_le32(b->p+16) || !nh_range(b,20,pm_le32(b->p+16))) return false;
 end=20+pm_le32(b->p+16);
 while(at<end) { if(!(at&4095U) && !nh_poll(b)) return false; if(!b->p[at++] && b->p[12]==1) { if(end-at<3 || !nh_u24(b->p+at)) return false; at+=3; } }
 if(!nh_emit(f,s,b,"tape-descriptor.bin",0,20) || !nh_emit(f,s,b,"pulses.tapdata",20,end-20)) { return false; } s->size=end; return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { nh_blob b; bool ok; if(!nh_load(f,&b,pd)) return false; ok=parse_blob(f,s,&b); xx_mem_free(b.p); return ok; }

void xx_commodore_tap_init(xx_commodore_tap *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r)); pm_init(&r->format,d,b,XX_FILE_TYPE_COMMODORE_TAP,"tap"); } }
xx_commodore_tap *xx_commodore_tap_create(xx_io_device *d,int64_t b) { xx_commodore_tap *r=(xx_commodore_tap *)xx_mem_alloc(sizeof(*r)); if(r) xx_commodore_tap_init(r,d,b); return r; }
void xx_commodore_tap_destroy(xx_commodore_tap *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_commodore_tap_free(xx_commodore_tap *r) { if(r) { xx_commodore_tap_destroy(r); xx_mem_free(r); } }
bool xx_commodore_tap_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_commodore_tap_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
