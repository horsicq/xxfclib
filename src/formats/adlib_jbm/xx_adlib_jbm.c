/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/adlib_jbm/xx_adlib_jbm.h"
#include "../xx_sixteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 m16_blob b={0};uint8_t *marks=NULL,used[255]={0};uint32_t refs[11],table,inst,first=65536,pool=65536,ns,ni,i;uint64_t at,start;bool ok=false;
 M16_NEED(m16_load(f,&b,pd)&&m16_span(&b,0,32)&&pm_le16(b.p)==2&&pm_le16(b.p+2)&&!(pm_le16(b.p+8)&~1U));table=pm_le16(b.p+4);inst=pm_le16(b.p+6);M16_NEED(table==32&&inst<b.n&&inst>32&&(b.n-inst)%16==0);ni=(uint32_t)((b.n-inst)/16);M16_NEED(ni&&ni<=256);
 for(i=0;i<11;++i){refs[i]=pm_le16(b.p+10+i*2);if(refs[i]){M16_NEED(refs[i]>=table&&refs[i]<inst);if(refs[i]<first)first=refs[i];}}M16_NEED(first<inst&&first>table&&!((first-table)&1));ns=(first-table)/2;M16_NEED(ns&&ns<=255);
 for(i=0;i<11;++i)if(refs[i]){bool end=false;at=refs[i];while(at<inst){uint8_t v=b.p[(size_t)at++];M16_NEED(m16_work(&b,1));if(v==255){end=true;break;}M16_NEED(v<ns);used[v]=1;}M16_NEED(end);}
 for(i=0;i<ns;++i)if(used[i]){uint32_t p=pm_le16(b.p+table+i*2);M16_NEED(p>first&&p<inst);if(p<pool)pool=p;}M16_NEED(pool<inst);for(i=0;i<11;++i)if(refs[i]){at=refs[i];M16_NEED(at<pool);while(at<pool&&b.p[(size_t)at]!=255){M16_NEED(m16_work(&b,1));++at;}M16_NEED(at<pool);}
 marks=(uint8_t *)xx_mem_alloc(inst);M16_NEED(marks);xx_mem_zero(marks,inst);M16_NEED(m16_emit(f,s,&b,"descriptor.jbm",0,32)&&m16_emit(f,s,&b,"sequence-index.jbm",table,first-table)&&m16_emit(f,s,&b,"voice-orders.jbm",first,pool-first));
 at=pool;while(at<inst){bool end=false;start=at;marks[(size_t)at]=1;while(at<inst){uint8_t c=b.p[(size_t)at++];M16_NEED(m16_work(&b,1));if(c==255){end=true;break;}if(c==253){M16_NEED(at<inst&&b.p[(size_t)at]<ni);++at;}else{M16_NEED((c&127)<=95&&at<=inst&&inst-at>=3&&b.p[(size_t)at]<=63);at+=3;}}M16_NEED(end&&m16_emit(f,s,&b,"sequence.jbm",start,at-start));}
 for(i=0;i<ns;++i)if(used[i])M16_NEED(marks[pm_le16(b.p+table+i*2)]);for(i=0;i<ni;++i)M16_NEED(m16_emit(f,s,&b,"instrument.jbm",inst+i*16,16));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(marks);xx_mem_free(b.p);return ok;
}
void xx_adlib_jbm_init(xx_adlib_jbm *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_JBM,"adlib_jbm");}}
xx_adlib_jbm *xx_adlib_jbm_create(xx_io_device *d,int64_t b) {xx_adlib_jbm *r=(xx_adlib_jbm *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_jbm_init(r,d,b);return r;}
void xx_adlib_jbm_destroy(xx_adlib_jbm *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_jbm_free(xx_adlib_jbm *r) {if(r){xx_adlib_jbm_destroy(r);xx_mem_free(r);}}
bool xx_adlib_jbm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_jbm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
