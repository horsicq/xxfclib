/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/asylum_amf/xx_asylum_amf.h"
#include "../asylum_amf/xx_thirteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 tm_blob b={0};uint64_t at;uint32_t ni,np,no,i,j,len[64];bool ok=false;
 TM_NEED(tm_load(f,&b,pd)&&tm_tag(&b,0,"ASYLUM Music Format V1.0\0\0\0\0\0\0\0\0",32)&&tm_span(&b,0,2662));
 ni=b.p[34];np=b.p[35];no=b.p[36];TM_NEED(b.p[32]&&b.p[32]<=31&&b.p[33]>=32&&ni&&ni<=64&&np&&no&&b.p[37]<no);
 for(i=0;i<no;++i)TM_NEED(b.p[38+i]<np);
 TM_NEED(tm_emit(f,s,&b,"descriptor.amf",0,38)&&tm_emit(f,s,&b,"orders.amf",38,256));
 for(i=0;i<ni;++i){const uint8_t *q=b.p+294+i*37;uint32_t a=pm_le32(q+29),z=pm_le32(q+33);len[i]=pm_le32(q+25);TM_NEED(len[i]<131072&&q[22]<=15&&q[23]<=64&&a<=len[i]&&z<=len[i]-a);}
 TM_NEED(tm_emit(f,s,&b,"instruments.amf",294,2368));at=2662;
 for(i=0;i<np;++i){TM_NEED(tm_span(&b,at,2048));for(j=0;j<512;++j){const uint8_t *q=b.p+(size_t)at+j*4;TM_NEED(tm_work(&b,1)&&q[0]<=114&&q[1]<=ni);}TM_NEED(tm_emit(f,s,&b,"pattern.amf",at,2048));at+=2048;}
 for(i=0;i<ni;++i)if(len[i]>1){TM_NEED(tm_emit(f,s,&b,"sample.pcm8",at,len[i]));at+=len[i];}
 TM_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_asylum_amf_init(xx_asylum_amf *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ASYLUM_AMF,"asylum_amf");}}
xx_asylum_amf *xx_asylum_amf_create(xx_io_device *d,int64_t b) {xx_asylum_amf *r=(xx_asylum_amf *)xx_mem_alloc(sizeof(*r));if(r)xx_asylum_amf_init(r,d,b);return r;}
void xx_asylum_amf_destroy(xx_asylum_amf *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_asylum_amf_free(xx_asylum_amf *r) {if(r){xx_asylum_amf_destroy(r);xx_mem_free(r);}}
bool xx_asylum_amf_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_asylum_amf_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
