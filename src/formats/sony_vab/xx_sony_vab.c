/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/sony_vab/xx_sony_vab.h"
#include "../nintendo_sdat/xx_twelfth_c.h"
#include "xxfclib/data/xx_data.h"

static bool read_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 const uint8_t *p=b->p;uint32_t programs,tones,samples,table,a,i,j,active=0,total=0;char label[64];
 if(b->n<32 || xx_rt_memcmp(p,"pBAV",4) || xx_data_get_u32(p+4, 4, 0, false)<5 || xx_data_get_u32(p+4, 4, 0, false)>7 || xx_data_get_u32(p+12, 4, 0, false)!=b->n || p[24]>127 || p[25]>127) return false;
 programs=xx_data_get_u16(p+18, 2, 0, false);tones=xx_data_get_u16(p+20, 2, 0, false);samples=xx_data_get_u16(p+22, 2, 0, false);
 if(!programs || programs>128 || !samples || samples>255 || !tones || tones>programs*16) return false;
 table=32+128*16+programs*16*32;a=table+512;if(!tc_span(b,0,a) || xx_data_get_u16(p+table, 2, 0, false)) return false;
 for(i=0;i<128;++i) {uint32_t q=32+i*16,n=p[q];if(!n) continue;if(n>16 || active>=programs || p[q+1]>127 || p[q+4]>127) return false;
  for(j=0;j<n;++j) {uint32_t t=32+128*16+active*512+j*32,sample=xx_data_get_u16(p+t+22, 2, 0, false);
   if(!tc_work(b,1) || p[t+2]>127 || p[t+3]>127 || p[t+4]>127 || p[t+6]>p[t+7] || p[t+7]>127 || xx_data_get_u16(p+t+20, 2, 0, false)!=i || !sample || sample>samples) return false;
  }++active;total+=n;
 }if(active!=programs || total!=tones || !tc_emit(f,s,b,"vab-bank-tables.bin",0,a)) return false;
 for(i=1;i<=samples;++i) {uint32_t z=(uint32_t)xx_data_get_u16(p+table+2*i, 2, 0, false)*8,k;if(!z || z%16 || !tc_span(b,a,z)) return false;
  for(k=0;k<z;k+=16) {if(!tc_work(b,1) || (p[a+k]>>4)>4 || (p[a+k]&15)>12 || p[a+k+1]>7) return false;}
  if(!(p[a+z-15]&1)) { return false; } xx_rt_snprintf(label,sizeof(label),"sample-%03u.spu-adpcm",i);if(!tc_emit(f,s,b,label,a,z)) return false;a+=z;
 }for(i=samples+1;i<256;++i) if(xx_data_get_u16(p+table+i*2, 2, 0, false)) return false;if(a!=b->n) return false;s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { tc_blob b;bool ok;if(!tc_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_sony_vab_init(xx_sony_vab *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SONY_VAB,"sony_vab");} }
xx_sony_vab *xx_sony_vab_create(xx_io_device *d,int64_t b) { xx_sony_vab *r=(xx_sony_vab *)xx_mem_alloc(sizeof(*r));if(r) xx_sony_vab_init(r,d,b);return r; }
void xx_sony_vab_destroy(xx_sony_vab *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_sony_vab_free(xx_sony_vab *r) { if(r) {xx_sony_vab_destroy(r);xx_mem_free(r);} }
bool xx_sony_vab_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_sony_vab_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
