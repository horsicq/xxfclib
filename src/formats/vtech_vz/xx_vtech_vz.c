/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://github.com/mamedev/mame/blob/master/src/mame/vtech/vtech1.cpp
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/vtech_vz/xx_vtech_vz.h"
#include "../atari_7800_a78/xx_eleventh_retro.h"
static bool read_components(Abstractformat *f,pm_stream *s,er_blob *b) {
 const uint8_t *p=b->p;char name[32];uint32_t start,z;
 if(b->n<=24 || xx_rt_memcmp(p,"VZF",3) || (p[3]!='0' && p[3]!='1') || p[20] || (p[21]!=0xf0 && p[21]!=0xf1) || !er_name(p+4,17,name,false)) return false;
 start=pm_le16(p+22);z=b->n-24;if(z>65536U-start) return false;
 if(!er_emit(f,s,b,"snapshot-descriptor.bin",0,24) || !er_emit(f,s,b,"original-program.bin",24,z)) return false;s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { er_blob b;bool ok;if(!er_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_vtech_vz_init(xx_vtech_vz *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_VTECH_VZ,"vtech_vz");} }
xx_vtech_vz *xx_vtech_vz_create(xx_io_device *d,int64_t b) { xx_vtech_vz *r=(xx_vtech_vz *)xx_mem_alloc(sizeof(*r));if(r) xx_vtech_vz_init(r,d,b);return r; }
void xx_vtech_vz_destroy(xx_vtech_vz *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_vtech_vz_free(xx_vtech_vz *r) { if(r) {xx_vtech_vz_destroy(r);xx_mem_free(r);} }
bool xx_vtech_vz_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_vtech_vz_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
