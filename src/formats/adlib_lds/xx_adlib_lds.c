/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/adlib_lds/xx_adlib_lds.h"
#include "../xx_fifteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 m15_blob b={0};uint64_t at=17,positions,patterns,start;uint32_t patches,np,i,len;bool ok=false;
 M15_NEED(m15_load(f,&b,pd)&&m15_span(&b,0,17)&&b.p[0]<=2&&pm_le16(b.p+1)&&b.p[3]&&b.p[4]);patches=pm_le16(b.p+15);len=b.p[4];M15_NEED(patches&&patches<=64&&m15_emit(f,s,&b,"descriptor.lds",0,17));
 for(i=0;i<patches;++i){M15_NEED(m15_emit(f,s,&b,"patch.lds",at,46));at+=46;}M15_NEED(m15_span(&b,at,2));start=at;np=pm_le16(b.p+(size_t)at);at+=2;M15_NEED(np&&np<=256&&m15_span(&b,at,(uint64_t)np*27+2));positions=at;at+=(uint64_t)np*27+2;patterns=at;M15_NEED(at<b.n&&!((b.n-at)&1)&&m15_emit(f,s,&b,"position-table.lds",start,at-start));
 for(i=0;i<np*9;++i){uint64_t off=pm_le16(b.p+(size_t)positions+i*3),p;unsigned rows=0;M15_NEED(!(off&1)&&off<b.n-patterns);p=patterns+off;while(rows<len){uint16_t cmd;uint8_t hi,lo;M15_NEED(m15_span(&b,p,2)&&m15_work(&b,1));cmd=pm_le16(b.p+(size_t)p);p+=2;hi=(uint8_t)(cmd>>8);lo=(uint8_t)cmd;if(hi==128){M15_NEED((unsigned)lo+1<=len-rows);rows+=(unsigned)lo+1;continue;}++rows;if(hi==249){M15_NEED(lo<np);break;}if(hi==250||hi==252)break;M15_NEED(hi<160||hi>=240);if(hi<128&&cmd){uint8_t tr=b.p[(size_t)positions+i*3+2];int transpose=(int)(tr&127);uint32_t idx;if(tr&64)transpose-=128;idx=(uint32_t)((int)lo+((tr&128)?transpose:0))&63U;M15_NEED(idx<patches);}}}
 M15_NEED(m15_emit(f,s,&b,"pattern-words.lds",patterns,b.n-patterns));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_lds_init(xx_adlib_lds *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_LDS,"adlib_lds");}}
xx_adlib_lds *xx_adlib_lds_create(xx_io_device *d,int64_t b) {xx_adlib_lds *r=(xx_adlib_lds *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_lds_init(r,d,b);return r;}
void xx_adlib_lds_destroy(xx_adlib_lds *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_lds_free(xx_adlib_lds *r) {if(r){xx_adlib_lds_destroy(r);xx_mem_free(r);}}
bool xx_adlib_lds_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_lds_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
