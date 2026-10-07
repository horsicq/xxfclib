/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://7800.8bitdev.org/index.php/A78_Header_Specification
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/atari_7800_a78/xx_atari_7800_a78.h"
#include "../atari_7800_a78/xx_eleventh_retro.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f,pm_stream *s,er_blob *b) {
 uint32_t z,i;const uint8_t *p=b->p;
 if(b->n<128 || p[0]<1 || p[0]>3 || xx_rt_memcmp(p+1,"ATARI7800",9) || xx_rt_memcmp(p+100,"ACTUAL CART DATA STARTS HERE",28)) return false;
 for(i=10;i<17;++i) if(p[i]!=0 && p[i]!=' ') return false;
 z=xx_data_get_u32(p+49, 4, 0, true);if(!z || z>b->n-128 || z+128!=b->n || p[55]>12 || p[56]>12 || p[57]>3 || p[58]>3) return false;
 if(!er_emit(f,s,b,"cartridge-descriptor.bin",0,128) || !er_emit(f,s,b,"cartridge-rom.bin",128,z)) { return false; } s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { er_blob b;bool ok;if(!er_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_atari_7800_a78_init(xx_atari_7800_a78 *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ATARI_7800_A78,"atari_7800_a78");} }
xx_atari_7800_a78 *xx_atari_7800_a78_create(xx_io_device *d,int64_t b) { xx_atari_7800_a78 *r=(xx_atari_7800_a78 *)xx_mem_alloc(sizeof(*r));if(r) xx_atari_7800_a78_init(r,d,b);return r; }
void xx_atari_7800_a78_destroy(xx_atari_7800_a78 *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_atari_7800_a78_free(xx_atari_7800_a78 *r) { if(r) {xx_atari_7800_a78_destroy(r);xx_mem_free(r);} }
bool xx_atari_7800_a78_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_atari_7800_a78_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
