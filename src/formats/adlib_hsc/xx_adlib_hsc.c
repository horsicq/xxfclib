/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/adlib_hsc/xx_adlib_hsc.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 music_blob b={0};uint64_t n,at;uint32_t i,j,np;bool active=false,term=false,ok=false;
 MUSIC_NEED(music_load(f,&b,pd)&&b.n>=2739&&b.n<=59188);n=b.n-1587;MUSIC_NEED(n%1152==0||(n%1152==1&&b.p[(size_t)b.n-1]==0));np=(uint32_t)(n/1152);MUSIC_NEED(np&&np<=50);
 for(i=0;i<128;++i){const uint8_t *q=b.p+i*12;MUSIC_NEED(music_work(&b,1)&&q[8]<=15&&q[9]<=3&&q[10]<=3);for(j=0;j<12;++j)if(q[j])active=true;}
 MUSIC_NEED(active);for(i=0;i<51;++i){uint8_t v=b.p[1536+i];if(!term){if(v>=0xb2)term=true;else if(v&128)MUSIC_NEED((v&127)<51);else MUSIC_NEED(v<np);}}MUSIC_NEED(term&&b.p[1536]<np);
 MUSIC_NEED(music_emit(f,s,&b,"instruments.hsc",0,1536)&&music_emit(f,s,&b,"orders.hsc",1536,51));
 for(i=0;i<np;++i){at=1587+(uint64_t)i*1152;for(j=0;j<576;++j){const uint8_t *q=b.p+(size_t)at+j*2;MUSIC_NEED(music_work(&b,1));if(q[0]&128)MUSIC_NEED(q[1]<128);else MUSIC_NEED(q[0]<=96||q[0]==127);}MUSIC_NEED(music_emit(f,s,&b,"pattern.hsc",at,1152));}
 if(n%1152) {MUSIC_NEED(music_emit(f,s,&b,"terminator.hsc",b.n-1,1)); } s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_hsc_init(xx_adlib_hsc *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_HSC,"adlib_hsc");}}
xx_adlib_hsc *xx_adlib_hsc_create(xx_io_device *d,int64_t b) {xx_adlib_hsc *r=(xx_adlib_hsc *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_hsc_init(r,d,b);return r;}
void xx_adlib_hsc_destroy(xx_adlib_hsc *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_hsc_free(xx_adlib_hsc *r) {if(r){xx_adlib_hsc_destroy(r);xx_mem_free(r);}}
bool xx_adlib_hsc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_hsc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
