/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Reference: https://github.com/mamedev/mame/blob/master/src/lib/formats/oric_tap.cpp
 * No payload execution, machine restoration or synthesized audio.
 */
#include "xxfclib/formats/oric_tap/xx_oric_tap.h"
#include "../atari_7800_a78/xx_eleventh_retro.h"
static bool read_components(Abstractformat *f,pm_stream *s,er_blob *b) {
 uint32_t a=0,count=0;const uint8_t *p=b->p;
 while(a<b->n) {uint32_t leader=a,h,n,start,end,z;char name[257];if(!er_poll(b) || ++count>1024) return false;
  while(a<b->n && p[a]==0x16) {if(a-leader>=4096) return false;++a;}if(a-leader<3 || !er_range(b,a,10) || p[a]!=0x24) return false;h=++a;
  if(p[h] || p[h+1] || (p[h+2]!=0 && p[h+2]!=0x80) || (p[h+3]!=0 && p[h+3]!=0x80 && p[h+3]!=0xc7) || p[h+8]) return false;
  end=pm_be16(p+h+4);start=pm_be16(p+h+6);if(end<start) return false;a=h+9;n=0;
  while(a<b->n && p[a]) {if(n>=255 || p[a]<32 || p[a]>126) return false;name[n++]=(char)p[a++];}if(a==b->n) return false;name[n]=0;++a;z=end-start+1;
  if(!er_emit(f,s,b,"tape-descriptor.bin",h,a-h) || !er_emit(f,s,b,n ? name:"unnamed-file.bin",a,z)) return false;a+=z;
 }s->size=b->n;return count!=0;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { er_blob b;bool ok;if(!er_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_oric_tap_init(xx_oric_tap *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ORIC_TAP,"oric_tap");} }
xx_oric_tap *xx_oric_tap_create(xx_io_device *d,int64_t b) { xx_oric_tap *r=(xx_oric_tap *)xx_mem_alloc(sizeof(*r));if(r) xx_oric_tap_init(r,d,b);return r; }
void xx_oric_tap_destroy(xx_oric_tap *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_oric_tap_free(xx_oric_tap *r) { if(r) {xx_oric_tap_destroy(r);xx_mem_free(r);} }
bool xx_oric_tap_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_oric_tap_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
