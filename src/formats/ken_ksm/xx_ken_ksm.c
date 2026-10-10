/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/ken_ksm/xx_ken_ksm.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 music_blob b={0};uint32_t n,i,last=0;bool active=false,ok=false;
 MUSIC_NEED(music_load(f,&b,pd)&&music_span(&b,0,82)&&music_zero_block(&b,48,16));n=xx_data_get_u16(b.p+80, 2, 0, false);MUSIC_NEED(n&&b.n==82+(uint64_t)n*4&&music_emit(f,s,&b,"track-descriptors.ksm",0,82));
 for(i=0;i<16;++i){MUSIC_NEED(b.p[16+i]&&b.p[16+i]<=240&&b.p[32+i]<=9&&b.p[64+i]<=63);active|=b.p[32+i]!=0;}MUSIC_NEED(active);
 for(i=0;i<n;++i){uint32_t c=xx_data_get_u32(b.p+82+(size_t)i*4, 4, 0, false),time=c>>12,track=(c>>8)&15;MUSIC_NEED(music_work(&b,1)&&time>=last&&b.p[32+track]);last=time;}MUSIC_NEED(last&&music_emit(f,s,&b,"packed-note-events.ksm",82,(uint64_t)n*4));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_ken_ksm_init(xx_ken_ksm *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_KEN_KSM,"ken_ksm");}}
xx_ken_ksm *xx_ken_ksm_create(xx_io_device *d,int64_t b) {xx_ken_ksm *r=(xx_ken_ksm *)xx_mem_alloc(sizeof(*r));if(r)xx_ken_ksm_init(r,d,b);return r;}
void xx_ken_ksm_destroy(xx_ken_ksm *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ken_ksm_free(xx_ken_ksm *r) {if(r){xx_ken_ksm_destroy(r);xx_mem_free(r);}}
bool xx_ken_ksm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_ken_ksm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
