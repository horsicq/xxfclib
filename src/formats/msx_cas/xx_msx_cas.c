/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://github.com/openMSX/openMSX/blob/master/src/cassette/CasImage.cc
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/msx_cas/xx_msx_cas.h"
#include "../atari_7800_a78/xx_eleventh_retro.h"
#include "xxfclib/data/xx_data.h"
static bool read_components(Abstractformat *f,pm_stream *s,er_blob *b) {
 static const uint8_t sig[]={0x1f,0xa6,0xde,0xba,0xcc,0x13,0x7d,0x74};uint32_t a=0,count=0;const uint8_t *p=b->p;
 while(a<b->n) {uint32_t i,start,end,run,z,after;char name[16];if(!er_poll(b) || ++count>1024 || !er_range(b,a,38) || xx_rt_memcmp(p+a,sig,8)) return false;
  for(i=0;i<10;++i) { if(p[a+8+i]!=0xd0) return false; } if(!er_name(p+a+18,6,name,false) || xx_rt_memcmp(p+a+24,sig,8)) return false;
  start=xx_data_get_u16(p+a+32, 2, 0, false);end=xx_data_get_u16(p+a+34, 2, 0, false);run=xx_data_get_u16(p+a+36, 2, 0, false);if(end<start || (run && (run<start || run>end))) return false;z=end-start+1;if(!er_range(b,a+38,z)) return false;
  if(!er_emit(f,s,b,"binary-descriptor.bin",a,38) || !er_emit(f,s,b,name,a+38,z)) { return false; } after=a+38+z;
  if(after<b->n && p[after]==0) {uint32_t rounded=(after+7U)&~7U;if(rounded>b->n || !er_zero(p+after,rounded-after)) return false;after=rounded;}
  a=after;
 }s->size=b->n;return count!=0;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { er_blob b;bool ok;if(!er_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_msx_cas_init(xx_msx_cas *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_MSX_CAS,"msx_cas");} }
xx_msx_cas *xx_msx_cas_create(xx_io_device *d,int64_t b) { xx_msx_cas *r=(xx_msx_cas *)xx_mem_alloc(sizeof(*r));if(r) xx_msx_cas_init(r,d,b);return r; }
void xx_msx_cas_destroy(xx_msx_cas *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_msx_cas_free(xx_msx_cas *r) { if(r) {xx_msx_cas_destroy(r);xx_mem_free(r);} }
bool xx_msx_cas_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_msx_cas_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
