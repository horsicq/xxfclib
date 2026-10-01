/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/adlib_dfm/xx_adlib_dfm.h"
#include "../xx_fifteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 m15_blob b={0};uint64_t at=905,start;uint32_t i,j,np;uint64_t seen=0;bool terminal=false,ok=false;
 M15_NEED(m15_load(f,&b,pd)&&m15_tag(&b,0,"DFM\032",4)&&m15_span(&b,0,905)&&b.p[4]==0&&b.p[6]<=32&&b.p[39]);np=b.p[904];M15_NEED(np&&np<=64&&m15_emit(f,s,&b,"descriptor.dfm",0,40)&&m15_emit(f,s,&b,"instrument-names.dfm",40,384));
 for(i=0;i<32;++i){M15_NEED(b.p[40+i*12]<=11&&m15_emit(f,s,&b,"instrument.dfm",424+i*11,11));}
 for(i=0;i<128;++i){uint8_t c=b.p[776+i];if(c==128)terminal=true;else if(!terminal)M15_NEED(c<np);}M15_NEED(terminal&&b.p[776]<np&&m15_emit(f,s,&b,"orders.dfm",776,129));
 for(i=0;i<np;++i){uint8_t id;start=at;M15_NEED(m15_span(&b,at,1));id=b.p[(size_t)at++];M15_NEED(id<np&&!(seen&(UINT64_C(1)<<id)));seen|=UINT64_C(1)<<id;for(j=0;j<576;++j){uint8_t note;M15_NEED(m15_span(&b,at,1)&&m15_work(&b,1));note=b.p[(size_t)at++];if(note&128){M15_NEED(m15_span(&b,at,1));++at;}}M15_NEED(m15_emit(f,s,&b,"pattern.dfm",start,at-start));}
 M15_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_dfm_init(xx_adlib_dfm *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_DFM,"adlib_dfm");}}
xx_adlib_dfm *xx_adlib_dfm_create(xx_io_device *d,int64_t b) {xx_adlib_dfm *r=(xx_adlib_dfm *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_dfm_init(r,d,b);return r;}
void xx_adlib_dfm_destroy(xx_adlib_dfm *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_dfm_free(xx_adlib_dfm *r) {if(r){xx_adlib_dfm_destroy(r);xx_mem_free(r);}}
bool xx_adlib_dfm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_dfm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
