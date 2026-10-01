/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/adlib_bnk/xx_adlib_bnk.h"
#include "../xx_fourteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 fm_blob b={0};fm_range ranges[1024];unsigned nr=0;uint32_t used,total,list,data,count,i,j,seen=0;bool ok=false;
 FM_NEED(fm_load(f,&b,pd)&&fm_span(&b,0,20)&&b.p[0]==1&&b.p[1]==0&&fm_tag(&b,2,"ADLIB-",6));used=pm_le16(b.p+8);total=pm_le16(b.p+10);list=pm_le32(b.p+12);data=pm_le32(b.p+16);
 FM_NEED(used&&used<=total&&total<=1000&&list>=20&&data>=list+(uint64_t)total*12&&data<=b.n&&(b.n-data)%30==0);count=(uint32_t)((b.n-data)/30);FM_NEED(count&&count<=1000);
 FM_NEED(fm_claim(&b,ranges,&nr,0,20,false)&&fm_claim(&b,ranges,&nr,list,(uint64_t)total*12,false)&&fm_claim(&b,ranges,&nr,data,(uint64_t)count*30,false)&&fm_emit(f,s,&b,"descriptor.bnk",0,20)&&fm_emit(f,s,&b,"names.bnk",list,(uint64_t)total*12));
 for(i=0;i<total;++i){const uint8_t *q=b.p+list+i*12;FM_NEED(fm_work(&b,1)&&q[2]<=1&&pm_le16(q)<count);if(q[2]){bool z=false;FM_NEED(q[3]);for(j=0;j<9;++j)if(!q[3+j])z=true;FM_NEED(z);++seen;}}
 FM_NEED(seen==used);for(i=0;i<count;++i){const uint8_t *q=b.p+data+i*30;FM_NEED(q[0]<=1&&q[1]<=10);FM_NEED(fm_emit(f,s,&b,"instrument.bnk",data+(uint64_t)i*30,30));}s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_bnk_init(xx_adlib_bnk *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_BNK,"adlib_bnk");}}
xx_adlib_bnk *xx_adlib_bnk_create(xx_io_device *d,int64_t b) {xx_adlib_bnk *r=(xx_adlib_bnk *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_bnk_init(r,d,b);return r;}
void xx_adlib_bnk_destroy(xx_adlib_bnk *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_bnk_free(xx_adlib_bnk *r) {if(r){xx_adlib_bnk_destroy(r);xx_mem_free(r);}}
bool xx_adlib_bnk_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_bnk_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
