/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/adlib_xsm/xx_adlib_xsm.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 music_blob b={0};uint32_t n,i,j;uint64_t at=8;bool ok=false;
 MUSIC_NEED(music_load(f,&b,pd)&&music_tag(&b,0,"ofTAZ!",6)&&music_span(&b,0,152));n=xx_data_get_u16(b.p+6, 2, 0, false);MUSIC_NEED(n&&n<=3200&&b.n==152+(uint64_t)n*9&&music_emit(f,s,&b,"descriptor.xsm",0,8));
 for(i=0;i<9;++i){MUSIC_NEED(music_emit(f,s,&b,"instrument.xsm",at,16));at+=16;}for(i=0;i<9;++i){for(j=0;j<n;++j)MUSIC_NEED(music_work(&b,1)&&b.p[(size_t)at+j]<=96);MUSIC_NEED(music_emit(f,s,&b,"note-stream.xsm",at,n));at+=n;}s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_xsm_init(xx_adlib_xsm *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_XSM,"adlib_xsm");}}
xx_adlib_xsm *xx_adlib_xsm_create(xx_io_device *d,int64_t b) {xx_adlib_xsm *r=(xx_adlib_xsm *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_xsm_init(r,d,b);return r;}
void xx_adlib_xsm_destroy(xx_adlib_xsm *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_xsm_free(xx_adlib_xsm *r) {if(r){xx_adlib_xsm_destroy(r);xx_mem_free(r);}}
bool xx_adlib_xsm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_xsm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
