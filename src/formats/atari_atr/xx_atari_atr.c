/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/atari_atr/xx_atari_atr.h"
#include "../nintendo_sdat/xx_twelfth_c.h"

static bool read_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 const uint8_t *p=b->p;uint32_t units,z,size,count,a=16,i;char label[64];
 if(b->n<16 || p[0]!=0x96 || p[1]!=2 || !tc_zero(p+8,7) || p[15]>1) return false;
 units=(uint32_t)pm_le16(p+2)|((uint32_t)pm_le16(p+6)<<16);size=pm_le16(p+4);
 if(!units || units>(TC_LIMIT-16)/16 || (size!=128 && size!=256)) { return false; } z=units*16;
 if(z!=b->n-16 || z<384 || (size==128 ? z%128 : (z-384)%256)) return false;
 count=size==128 ? z/128 : 3+(z-384)/256;if(count>4095) return false;
 if(!tc_emit(f,s,b,"atr-descriptor.bin",0,16)) return false;
 for(i=1;i<=count;++i) {uint32_t n=i<=3 ? 128:size;xx_rt_snprintf(label,sizeof(label),"sector-%04u.bin",i);if(!tc_emit(f,s,b,label,a,n)) return false;a+=n;}
 if(a!=b->n) { return false; } s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { tc_blob b;bool ok;if(!tc_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_atari_atr_init(xx_atari_atr *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ATARI_ATR,"atari_atr");} }
xx_atari_atr *xx_atari_atr_create(xx_io_device *d,int64_t b) { xx_atari_atr *r=(xx_atari_atr *)xx_mem_alloc(sizeof(*r));if(r) xx_atari_atr_init(r,d,b);return r; }
void xx_atari_atr_destroy(xx_atari_atr *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_atari_atr_free(xx_atari_atr *r) { if(r) {xx_atari_atr_destroy(r);xx_mem_free(r);} }
bool xx_atari_atr_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_atari_atr_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
