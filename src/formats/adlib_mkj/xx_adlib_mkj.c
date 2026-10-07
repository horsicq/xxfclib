/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/adlib_mkj/xx_adlib_mkj.h"
#include "../xx_fifteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 m15_blob b={0};uint64_t at=12;uint32_t ch,notes,i;bool active=false,ok=false;
 M15_NEED(m15_load(f,&b,pd)&&m15_tag(&b,0,"MKJamz",6)&&m15_span(&b,0,12)&&xx_data_get_u32(b.p+6, 4, 0, false)==0x3f800000U);ch=xx_data_get_u16(b.p+10, 2, 0, false);M15_NEED(ch&&ch<=9&&m15_emit(f,s,&b,"descriptor.mkj",0,12));
 for(i=0;i<ch;++i){unsigned j;M15_NEED(m15_span(&b,at,16));for(j=0;j<8;++j)M15_NEED(xx_data_get_u16(b.p+(size_t)at+j*2, 2, 0, false)<=255);M15_NEED(m15_emit(f,s,&b,"instrument.mkj",at,16));at+=16;}
 M15_NEED(m15_span(&b,at,2+(uint64_t)ch*2));notes=xx_data_get_u16(b.p+(size_t)at, 2, 0, false);M15_NEED(notes&&notes<=32767U/(ch+1));for(i=0;i<ch;++i){uint16_t flag=xx_data_get_u16(b.p+(size_t)at+2+i*2, 2, 0, false);M15_NEED(flag<=1);active|=flag!=0;}M15_NEED(active&&m15_emit(f,s,&b,"channel-table.mkj",at,2+(uint64_t)ch*2));at+=2+(uint64_t)ch*2;M15_NEED(b.n-at==(uint64_t)(ch+1)*notes*2);for(i=0;i<(ch+1)*notes;++i)M15_NEED(m15_work(&b,1)&&xx_data_get_u16(b.p+(size_t)at+i*2, 2, 0, false)<=32767);M15_NEED(m15_emit(f,s,&b,"event-grid.mkj",at,b.n-at));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_mkj_init(xx_adlib_mkj *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_MKJ,"adlib_mkj");}}
xx_adlib_mkj *xx_adlib_mkj_create(xx_io_device *d,int64_t b) {xx_adlib_mkj *r=(xx_adlib_mkj *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_mkj_init(r,d,b);return r;}
void xx_adlib_mkj_destroy(xx_adlib_mkj *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_mkj_free(xx_adlib_mkj *r) {if(r){xx_adlib_mkj_destroy(r);xx_mem_free(r);}}
bool xx_adlib_mkj_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_mkj_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
