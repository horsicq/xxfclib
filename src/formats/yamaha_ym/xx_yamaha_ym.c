/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/yamaha_ym/xx_yamaha_ym.h"
#include "../nintendo_sdat/xx_twelfth_c.h"

static bool read_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 const uint8_t *p=b->p;uint32_t frames,attrs,drums,at,i,start,z;char label[64];
 if(b->n<34 || (xx_rt_memcmp(p,"YM5!",4) && xx_rt_memcmp(p,"YM6!",4)) || xx_rt_memcmp(p+4,"LeOnArD!",8)) return false;
 frames=pm_be32(p+12);attrs=pm_be32(p+16);drums=pm_be16(p+20);at=34+pm_be16(p+32);
 if(!frames || frames>1000000 || attrs&~7U || drums>256 || !pm_be32(p+22) || !pm_be16(p+26) || pm_be32(p+28)>=frames || !tc_span(b,0,at)) return false;
 if(!tc_emit(f,s,b,"ym-descriptor.bin",0,at)) return false;
 for(i=0;i<drums;++i) {if(!tc_span(b,at,4)) return false;z=pm_be32(p+at);at+=4;xx_rt_snprintf(label,sizeof(label),"digidrum-%03u.bin",i);
  if(!z || !tc_emit(f,s,b,label,at,z)) return false;at+=z;}
 start=at;for(i=0;i<3;++i) {if(!tc_string(b,at,b->n,&z,false)) return false;at+=z;}
 if(!tc_emit(f,s,b,"song-metadata.bin",start,at-start) || !tc_span(b,at,frames*16) || b->n-at!=frames*16+4 || xx_rt_memcmp(p+b->n-4,"End!",4)) return false;
 if(attrs&1) {for(i=0;i<16;++i) {xx_rt_snprintf(label,sizeof(label),"register-%02u.bin",i);if(!tc_emit(f,s,b,label,at+i*frames,frames)) return false;}}
 else if(!tc_emit(f,s,b,"register-frames.bin",at,frames*16)) return false;s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { tc_blob b;bool ok;if(!tc_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_yamaha_ym_init(xx_yamaha_ym *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_YAMAHA_YM,"yamaha_ym");} }
xx_yamaha_ym *xx_yamaha_ym_create(xx_io_device *d,int64_t b) { xx_yamaha_ym *r=(xx_yamaha_ym *)xx_mem_alloc(sizeof(*r));if(r) xx_yamaha_ym_init(r,d,b);return r; }
void xx_yamaha_ym_destroy(xx_yamaha_ym *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_yamaha_ym_free(xx_yamaha_ym *r) { if(r) {xx_yamaha_ym_destroy(r);xx_mem_free(r);} }
bool xx_yamaha_ym_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_yamaha_ym_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
