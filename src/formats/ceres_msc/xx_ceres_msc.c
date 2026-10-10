/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/ceres_msc/xx_ceres_msc.h"
#include "../common/xx_music_components.h"
#include "xxfclib/data/xx_data.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 music_blob b={0};uint8_t *decoded=NULL;uint64_t at=88,start,total=0;uint32_t blocks,cap,i;bool ok=false;
 MUSIC_NEED(music_load(f,&b,pd)&&music_tag(&b,0,"Ceres \023 MSCplay ",16)&&music_span(&b,0,88)&&!xx_data_get_u16(b.p+16, 2, 0, false)&&xx_data_get_u16(b.p+82, 2, 0, false));blocks=xx_data_get_u16(b.p+84, 2, 0, false);cap=xx_data_get_u16(b.p+86, 2, 0, false);MUSIC_NEED(blocks&&blocks<=4096&&cap&&music_emit(f,s,&b,"descriptor.msc",0,88));decoded=(uint8_t *)xx_mem_alloc(cap);MUSIC_NEED(decoded);
 for(i=0;i<blocks;++i){uint64_t end;uint32_t out=0;start=at;MUSIC_NEED(music_span(&b,at,2));end=at+2+xx_data_get_u16(b.p+(size_t)at, 2, 0, false);at+=2;MUSIC_NEED(end>at&&end<=b.n);
  while(at<end){uint8_t c=b.p[(size_t)at++];uint32_t n=1,dist=0;MUSIC_NEED(music_work(&b,1));if(c==155||c==175){uint8_t v;MUSIC_NEED(at<end);v=b.p[(size_t)at++];if(v){n=(v&15)+2;dist=(v>>4)+1;if(c==175){MUSIC_NEED(at<end);dist=(v>>4)+17+(uint32_t)b.p[(size_t)at++]*16;++n;}if((v&15)==15){MUSIC_NEED(at<end);n+=b.p[(size_t)at++];}MUSIC_NEED(dist&&dist<=cap);}}
   MUSIC_NEED(n<=cap-out&&total<=67108864-n&&music_work(&b,n));total+=n;while(n--){decoded[out]=dist?(out>=dist?decoded[out-dist]:0):c;++out;}}
  MUSIC_NEED(out&&!(out&1)&&music_emit(f,s,&b,"encoded-commands.msc",start,at-start));}
 MUSIC_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(decoded);xx_mem_free(b.p);return ok;
}
void xx_ceres_msc_init(xx_ceres_msc *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_CERES_MSC,"ceres_msc");}}
xx_ceres_msc *xx_ceres_msc_create(xx_io_device *d,int64_t b) {xx_ceres_msc *r=(xx_ceres_msc *)xx_mem_alloc(sizeof(*r));if(r)xx_ceres_msc_init(r,d,b);return r;}
void xx_ceres_msc_destroy(xx_ceres_msc *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ceres_msc_free(xx_ceres_msc *r) {if(r){xx_ceres_msc_destroy(r);xx_mem_free(r);}}
bool xx_ceres_msc_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_ceres_msc_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
