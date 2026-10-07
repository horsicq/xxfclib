/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/tracker_dtt/xx_tracker_dtt.h"
#include "../nintendo_sdat/xx_twelfth_c.h"
#include "xxfclib/data/xx_data.h"

static bool read_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 uint32_t ch,orders,patterns,samples,at,i,j,n,base,rows,end,maxend,count=0;tc_extent ext[4096];const uint8_t *p=b->p;char label[64];
 if(b->n<168 || xx_rt_memcmp(p,"DskT",4) || xx_data_get_u32(p+132, 4, 0, false)!=0) return false;
 ch=xx_data_get_u32(p+136, 4, 0, false);orders=xx_data_get_u32(p+140, 4, 0, false);patterns=xx_data_get_u32(p+160, 4, 0, false);samples=xx_data_get_u32(p+164, 4, 0, false);
 if(!ch || ch>8 || !orders || orders>256 || !patterns || patterns>256 || samples>63 || !xx_data_get_u32(p+152, 4, 0, false) || xx_data_get_u32(p+152, 4, 0, false)>255 || xx_data_get_u32(p+156, 4, 0, false)>=orders) return false;
 for(i=0;i<8;++i) if(p[144+i]>7) return false;
 at=168+((orders+3)&~3U);if(!tc_span(b,168,at-168) || !tc_zero(p+168+orders,at-168-orders)) return false;
 for(i=0;i<orders;++i) if(p[168+i]>=patterns) return false;
 base=at+patterns*4+((patterns+3)&~3U);end=base+samples*64;if(!tc_span(b,at,end-at) || !tc_zero(p+at+patterns*4+patterns,((patterns+3)&~3U)-patterns)) return false;
 maxend=end;if(!tc_claim(b,ext,&count,0,end,false) || !tc_emit(f,s,b,"module-index.bin",0,end)) return false;
 for(i=0;i<patterns;++i) {n=xx_data_get_u32(p+at+i*4, 4, 0, false);rows=p[at+patterns*4+i];if(!rows || n<end || n&3 || !tc_span(b,n,4)) return false;
  j=n;while(rows--) {uint32_t c;for(c=0;c<ch;++c) {uint32_t v;if(!tc_work(b,1) || !tc_span(b,j,4)) return false;v=xx_data_get_u32(p+j, 4, 0, false);if((v&63)>samples) return false;j+=4;if(v&(31U<<17)) {if(!tc_span(b,j,4)) return false;j+=4;}}}
  if(!tc_claim(b,ext,&count,n,j-n,false)) { return false; } xx_rt_snprintf(label,sizeof(label),"pattern-%03u.bin",i);if(!tc_emit(f,s,b,label,n,j-n)) return false;if(j>maxend) maxend=j;
 }
 for(i=0;i<samples;++i) {uint32_t q=base+i*64,size=xx_data_get_u32(p+q+24, 4, 0, false),off=xx_data_get_u32(p+q+60, 4, 0, false),ls=xx_data_get_u32(p+q+16, 4, 0, false),lz=xx_data_get_u32(p+q+20, 4, 0, false),ss=xx_data_get_u32(p+q+8, 4, 0, false),sz=xx_data_get_u32(p+q+12, 4, 0, false);
  if(p[q]>63 || p[q+1]>128 || xx_data_get_u16(p+q+2, 2, 0, false) || ls>size || lz>size-ls || ss>size || sz>size-ss) return false;
  if(!size) {if(off) return false;continue;}if(off<end || !tc_claim(b,ext,&count,off,size,false)) return false;
  xx_rt_snprintf(label,sizeof(label),"sample-%02u.vidc",i);if(!tc_emit(f,s,b,label,off,size)) return false;if(off+size>maxend) maxend=off+size;
 }
 if(maxend!=b->n) { return false; } s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { tc_blob b;bool ok;if(!tc_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_tracker_dtt_init(xx_tracker_dtt *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_DTT,"tracker_dtt");} }
xx_tracker_dtt *xx_tracker_dtt_create(xx_io_device *d,int64_t b) { xx_tracker_dtt *r=(xx_tracker_dtt *)xx_mem_alloc(sizeof(*r));if(r) xx_tracker_dtt_init(r,d,b);return r; }
void xx_tracker_dtt_destroy(xx_tracker_dtt *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_tracker_dtt_free(xx_tracker_dtt *r) { if(r) {xx_tracker_dtt_destroy(r);xx_mem_free(r);} }
bool xx_tracker_dtt_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_tracker_dtt_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
