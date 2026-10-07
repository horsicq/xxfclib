/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/gameboy_gbs/xx_gameboy_gbs.h"
#include "../snes_spc/xx_tenth_retro.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f,pm_stream *s,th_blob *b) {
 const uint8_t *p=b->p;unsigned i;
 if(b->n<=112 || b->n>4194416 || xx_rt_memcmp(p,"GBS\1",4) || !p[4] || !p[5] || p[5]>p[4] || (p[15]&0x78)) return false;
 for(i=6;i<=10;i+=2) {unsigned address=xx_data_get_u16(p+i, 2, 0, false);if(address<0x400 || address>0x7fff) return false;}
 if(!th_emit(f,s,b,"music-descriptor.bin",0,112) || !th_emit(f,s,b,"program.bin",112,b->n-112)) { return false; } s->size=b->n;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 th_blob b;bool ok;if(!th_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok;
}
void xx_gameboy_gbs_init(xx_gameboy_gbs *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_GAMEBOY_GBS,"gameboy_gbs"); } }
xx_gameboy_gbs *xx_gameboy_gbs_create(xx_io_device *d,int64_t b) { xx_gameboy_gbs *r=(xx_gameboy_gbs *)xx_mem_alloc(sizeof(*r));if(r) xx_gameboy_gbs_init(r,d,b);return r; }
void xx_gameboy_gbs_destroy(xx_gameboy_gbs *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_gameboy_gbs_free(xx_gameboy_gbs *r) { if(r) {xx_gameboy_gbs_destroy(r);xx_mem_free(r);} }
bool xx_gameboy_gbs_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_gameboy_gbs_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
