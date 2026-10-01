/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/tracker_soundfx/xx_tracker_soundfx.h"
#include "../asylum_amf/xx_thirteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 tm_blob b={0};uint32_t ni,i,j,np=0,no,lens[31];uint64_t at;bool ok=false;
 TM_NEED(tm_load(f,&b,pd));if(tm_tag(&b,60,"SONG",4))ni=15;else {TM_NEED(tm_tag(&b,124,"SONG",4));ni=31;}
 at=ni*4;TM_NEED(tm_span(&b,at,20+ni*30+130)&&pm_be16(b.p+(size_t)at+4)>=178);
 for(i=0;i<ni;++i){const uint8_t *q=b.p+(size_t)at+20+i*30;uint32_t a=pm_be16(q+26),z=(uint32_t)pm_be16(q+28)*2;lens[i]=pm_be32(b.p+i*4);TM_NEED(lens[i]<=16777216&&q[24]<=15&&q[25]<=64);if(z>2)TM_NEED(a<=lens[i]&&z<=lens[i]-a);}
 TM_NEED(tm_emit(f,s,&b,"sample-lengths.sfx",0,ni*4)&&tm_emit(f,s,&b,"descriptor.sfx",at,20)&&tm_emit(f,s,&b,"instruments.sfx",at+20,ni*30));at+=20+ni*30;no=b.p[(size_t)at];TM_NEED(no&&no<=127&&(b.p[(size_t)at+1]<no||b.p[(size_t)at+1]==127));for(i=0;i<no;++i){uint32_t v=b.p[(size_t)at+2+i];TM_NEED(v<128);if(v>=np)np=v+1;}
 TM_NEED(tm_emit(f,s,&b,"orders.sfx",at,130));at+=130;
 for(i=0;i<np;++i){TM_NEED(tm_span(&b,at,1024));for(j=0;j<256;++j){const uint8_t *q=b.p+(size_t)at+j*4;TM_NEED(tm_work(&b,1)&&(uint32_t)((q[0]&240)|(q[2]>>4))<=ni);}TM_NEED(tm_emit(f,s,&b,"pattern.sfx",at,1024));at+=1024;}
 for(i=0;i<ni;++i)if(lens[i]>2){TM_NEED(tm_emit(f,s,&b,"sample.pcm8",at,lens[i]));at+=lens[i];}
 TM_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_tracker_soundfx_init(xx_tracker_soundfx *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_SOUNDFX,"tracker_soundfx");}}
xx_tracker_soundfx *xx_tracker_soundfx_create(xx_io_device *d,int64_t b) {xx_tracker_soundfx *r=(xx_tracker_soundfx *)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_soundfx_init(r,d,b);return r;}
void xx_tracker_soundfx_destroy(xx_tracker_soundfx *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_soundfx_free(xx_tracker_soundfx *r) {if(r){xx_tracker_soundfx_destroy(r);xx_mem_free(r);}}
bool xx_tracker_soundfx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tracker_soundfx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
