/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/creative_cmf/xx_creative_cmf.h"
#include "../xx_fifteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 m15_blob b={0};uint64_t at,inst,music,head,start;uint32_t ver,num,i,delta;uint8_t running=0;bool ended=false,ok=false;
 M15_NEED(m15_load(f,&b,pd)&&m15_tag(&b,0,"CTMF",4)&&m15_span(&b,0,37));ver=xx_data_get_u16(b.p+4, 2, 0, false);M15_NEED(ver==0x100||ver==0x101);head=ver==0x100?37:40;M15_NEED(m15_span(&b,0,head)&&xx_data_get_u16(b.p+10, 2, 0, false)&&xx_data_get_u16(b.p+12, 2, 0, false));inst=xx_data_get_u16(b.p+6, 2, 0, false);music=xx_data_get_u16(b.p+8, 2, 0, false);num=ver==0x100?b.p[36]:xx_data_get_u16(b.p+36, 2, 0, false);M15_NEED(num&&num<=128&&inst>=head&&music>=inst&&num*16U<=music-inst&&m15_span(&b,inst,num*16U)&&music<b.n);
 for(i=0;i<16;++i)M15_NEED(b.p[20+i]<=1);
 for(i=0;i<3;++i){uint64_t z=xx_data_get_u16(b.p+14+i*2, 2, 0, false);if(z)M15_NEED(z>=head&&z<inst&&m15_z(&b,&z,inst));}
 M15_NEED(m15_emit(f,s,&b,"descriptor.cmf",0,head));if(inst>head)M15_NEED(m15_emit(f,s,&b,"metadata.cmf",head,inst-head));for(i=0;i<num;++i)M15_NEED(m15_emit(f,s,&b,"instrument.cmf",inst+i*16,16));if(music>inst+num*16U)M15_NEED(m15_emit(f,s,&b,"instrument-padding.cmf",inst+num*16U,music-inst-num*16U));
 at=music;while(at<b.n){uint8_t c;unsigned need;M15_NEED(m15_vlq(&b,&at,&delta)&&m15_span(&b,at,1)&&m15_work(&b,1));c=b.p[(size_t)at];if(c&128){++at;if(c<240)running=c;else running=0;}else{M15_NEED(running);c=running;}if(c<240){M15_NEED(c>=128);need=(c&240)==192||(c&240)==208?1:2;M15_NEED(m15_span(&b,at,need));for(i=0;i<need;++i)M15_NEED(b.p[(size_t)(at+i)]<128);at+=need;}
 else if(c==240||c==247){M15_NEED(m15_vlq(&b,&at,&delta)&&m15_span(&b,at,delta));at+=delta;}
 else if(c==255){uint8_t type;M15_NEED(m15_span(&b,at,1));type=b.p[(size_t)at++];M15_NEED(type<128&&m15_vlq(&b,&at,&delta)&&m15_span(&b,at,delta));if(type==47){M15_NEED(!delta);ended=true;break;}if(type==81)M15_NEED(delta==3);if(type==88)M15_NEED(delta==4);if(type==89)M15_NEED(delta==2);at+=delta;}else M15_NEED(false);}
 M15_NEED(ended);start=at;M15_NEED(at==b.n||(b.n-at==1&&b.p[(size_t)at]==255));M15_NEED(m15_emit(f,s,&b,"midi-events.cmf",music,start-music));if(start<b.n)M15_NEED(m15_emit(f,s,&b,"legacy-terminator.cmf",start,1));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_creative_cmf_init(xx_creative_cmf *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_CREATIVE_CMF,"creative_cmf");}}
xx_creative_cmf *xx_creative_cmf_create(xx_io_device *d,int64_t b) {xx_creative_cmf *r=(xx_creative_cmf *)xx_mem_alloc(sizeof(*r));if(r)xx_creative_cmf_init(r,d,b);return r;}
void xx_creative_cmf_destroy(xx_creative_cmf *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_creative_cmf_free(xx_creative_cmf *r) {if(r){xx_creative_cmf_destroy(r);xx_mem_free(r);}}
bool xx_creative_cmf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_creative_cmf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
