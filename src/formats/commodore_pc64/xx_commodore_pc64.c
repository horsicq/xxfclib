/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://vice-emu.sourceforge.io/vice_17.html#SEC382
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/commodore_pc64/xx_commodore_pc64.h"
#include "../common/xx_retro_tape_components.h"
static bool read_components(Abstractformat *f,pm_stream *s,retro_tape_blob *b) {
 const uint8_t *p=b->p;char name[32];uint32_t z;
 if(b->n<=26 || xx_rt_memcmp(p,"C64File",8) || p[24] || p[25]==255 || !retro_tape_name(p+8,16,name,true)) return false;
 z=b->n-26;if(p[25] && z%p[25]) return false;
 if(!retro_tape_emit(f,s,b,"pc64-descriptor.bin",0,26) || !retro_tape_emit(f,s,b,"original-file.bin",26,z)) { return false; } s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { retro_tape_blob b;bool ok;if(!retro_tape_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_commodore_pc64_init(xx_commodore_pc64 *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_COMMODORE_PC64,"commodore_pc64");} }
xx_commodore_pc64 *xx_commodore_pc64_create(xx_io_device *d,int64_t b) { xx_commodore_pc64 *r=(xx_commodore_pc64 *)xx_mem_alloc(sizeof(*r));if(r) xx_commodore_pc64_init(r,d,b);return r; }
void xx_commodore_pc64_destroy(xx_commodore_pc64 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_commodore_pc64_free(xx_commodore_pc64 *r) { if(r) {xx_commodore_pc64_destroy(r);xx_mem_free(r);} }
bool xx_commodore_pc64_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_commodore_pc64_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
