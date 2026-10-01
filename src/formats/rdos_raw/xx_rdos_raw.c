/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/rdos_raw/xx_rdos_raw.h"
#include "../xx_sixteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 m16_blob b={0};uint64_t at=10,start;bool stop=false,ok=false;
 M16_NEED(m16_load(f,&b,pd)&&m16_tag(&b,0,"RAWADATA",8)&&m16_span(&b,0,12)&&pm_le16(b.p+8)&&m16_emit(f,s,&b,"descriptor.raw",0,10));
 while(at<b.n){uint8_t value,command;M16_NEED(m16_span(&b,at,2)&&m16_work(&b,1));value=b.p[(size_t)at];command=b.p[(size_t)at+1];at+=2;if(value==255&&command==255){stop=true;break;}if(command==2){if(!value){M16_NEED(m16_span(&b,at,2)&&pm_le16(b.p+(size_t)at));at+=2;}else M16_NEED(value<=2);}}
 M16_NEED(stop&&m16_emit(f,s,&b,"opl-commands.raw",10,at-10));if(at<b.n){start=at;M16_NEED(b.p[(size_t)at++]==26&&m16_z(&b,&at,b.n,40));if(at<b.n&&b.p[(size_t)at]==27){++at;M16_NEED(m16_z(&b,&at,b.n,40));}if(at<b.n&&b.p[(size_t)at]==28){++at;M16_NEED(m16_z(&b,&at,b.n,1023));}M16_NEED(at==b.n&&m16_emit(f,s,&b,"metadata.raw",start,at-start));}
 M16_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_rdos_raw_init(xx_rdos_raw *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_RDOS_RAW,"rdos_raw");}}
xx_rdos_raw *xx_rdos_raw_create(xx_io_device *d,int64_t b) {xx_rdos_raw *r=(xx_rdos_raw *)xx_mem_alloc(sizeof(*r));if(r)xx_rdos_raw_init(r,d,b);return r;}
void xx_rdos_raw_destroy(xx_rdos_raw *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_rdos_raw_free(xx_rdos_raw *r) {if(r){xx_rdos_raw_destroy(r);xx_mem_free(r);}}
bool xx_rdos_raw_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_rdos_raw_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
