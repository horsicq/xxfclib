/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/adlib_bmf/xx_adlib_bmf.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 music_blob b={0};uint64_t at=6,start;uint32_t flags,i;bool ok=false;
 MUSIC_NEED(music_load(f,&b,pd)&&music_tag(&b,0,"BMF1.1",6)&&music_zstring_short(&b,&at,b.n)&&music_zstring_short(&b,&at,b.n)&&music_span(&b,at,5)&&b.p[(size_t)at]);++at;flags=xx_data_get_u32(b.p+(size_t)at, 4, 0, true);at+=4;MUSIC_NEED(music_emit(f,s,&b,"descriptor.bmf",0,at));
 for(i=0;i<32;++i)if(flags&(0x80000000U>>i)){MUSIC_NEED(music_emit(f,s,&b,"instrument.bmf",at,24));at+=24;}
 MUSIC_NEED(music_span(&b,at,4));flags=xx_data_get_u32(b.p+(size_t)at, 4, 0, true);MUSIC_NEED(flags&&!(flags&0x007fffffU)&&music_emit(f,s,&b,"stream-flags.bmf",at,4));at+=4;
 for(i=0;i<9;++i)if(flags&(0x80000000U>>i)){unsigned events=0;bool loop=false,ended=false;start=at;while(at<b.n){uint8_t c;MUSIC_NEED(++events<=1024&&music_work(&b,1));c=b.p[(size_t)at++];if(c==254){ended=true;break;}if(c==252){MUSIC_NEED(music_span(&b,at,1));++at;loop=true;continue;}if(c==125){MUSIC_NEED(loop);continue;}if(!(c&128))continue;MUSIC_NEED(music_span(&b,at,1));c=b.p[(size_t)at];if(c&128){++at;if(!(c&64))continue;MUSIC_NEED(music_span(&b,at,1));c=b.p[(size_t)at];}MUSIC_NEED(c>=32&&c<128);++at;}MUSIC_NEED(ended&&music_emit(f,s,&b,"channel-stream.bmf",start,at-start));}
 MUSIC_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_bmf_init(xx_adlib_bmf *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_BMF,"adlib_bmf");}}
xx_adlib_bmf *xx_adlib_bmf_create(xx_io_device *d,int64_t b) {xx_adlib_bmf *r=(xx_adlib_bmf *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_bmf_init(r,d,b);return r;}
void xx_adlib_bmf_destroy(xx_adlib_bmf *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_bmf_free(xx_adlib_bmf *r) {if(r){xx_adlib_bmf_destroy(r);xx_mem_free(r);}}
bool xx_adlib_bmf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_bmf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
