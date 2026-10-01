/* SPDX-License-Identifier: MIT. Original validated components; no playback/emulation. */
#include "xxfclib/formats/tracker_stx/xx_tracker_stx.h"
#include "../asylum_amf/xx_thirteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 tm_blob b={0};tm_range ranges[1024];unsigned nr=0;uint64_t pt,it,ct,maximum=64;uint32_t np,ni,no,i,j,lens[128],sample_at[128];bool broken=false,ok=false;
 TM_NEED(tm_load(f,&b,pd)&&tm_span(&b,0,64)&&(tm_tag(&b,20,"!Scream!",8)||tm_tag(&b,20,"BMOD2STM",8))&&tm_tag(&b,60,"SCRM",4));
 np=pm_le16(b.p+48);ni=pm_le16(b.p+50);no=pm_le16(b.p+52);TM_NEED(np&&np<=254&&ni&&ni<=128&&no&&no<=256&&b.p[42]<=64&&(b.p[43]>>4));
 pt=(uint64_t)pm_le16(b.p+32)*16;it=(uint64_t)pm_le16(b.p+34)*16;ct=(uint64_t)pm_le16(b.p+36)*16;
 TM_NEED(tm_claim(&b,ranges,&nr,0,64)&&tm_claim(&b,ranges,&nr,pt,np*2)&&tm_claim(&b,ranges,&nr,it,ni*2)&&tm_claim(&b,ranges,&nr,ct,32+no*5));
 TM_NEED(tm_emit(f,s,&b,"descriptor.stx",0,64)&&tm_emit(f,s,&b,"pattern-index.stx",pt,np*2)&&tm_emit(f,s,&b,"instrument-index.stx",it,ni*2)&&tm_emit(f,s,&b,"channels-orders.stx",ct,32+no*5));
 if(pt+np*2>maximum)maximum=pt+np*2;if(it+ni*2>maximum)maximum=it+ni*2;if(ct+32+no*5>maximum)maximum=ct+32+no*5;
 for(i=0;i<no;++i)TM_NEED(b.p[(size_t)ct+32+i*5]<np||(i+1==no&&b.p[(size_t)ct+32+i*5]==255));
 for(i=0;i<ni;++i){uint64_t q=(uint64_t)pm_le16(b.p+(size_t)it+i*2)*16;uint32_t a,z;TM_NEED(tm_claim(&b,ranges,&nr,q,80));a=pm_le32(b.p+(size_t)q+20);z=pm_le32(b.p+(size_t)q+24);lens[i]=pm_le32(b.p+(size_t)q+16);sample_at[i]=(uint32_t)pm_le16(b.p+(size_t)q+14)*16;TM_NEED((b.p[(size_t)q]==1||(!b.p[(size_t)q]&&!lens[i]))&&b.p[(size_t)q+28]<=64&&!b.p[(size_t)q+30]&&!(b.p[(size_t)q+31]&~1U)&&lens[i]<=16777216);if(z&&z!=65535)TM_NEED(a<=z&&z<=lens[i]);TM_NEED(tm_emit(f,s,&b,"instrument.stx",q,80));if(q+80>maximum)maximum=q+80;}
 {uint64_t q=(uint64_t)pm_le16(b.p+(size_t)pt)*16;TM_NEED(tm_span(&b,q,2));broken=pm_le16(b.p+(size_t)q)==pm_le16(b.p+28);}
 for(i=0;i<np;++i){uint64_t begin=(uint64_t)pm_le16(b.p+(size_t)pt+i*2)*16,at=begin;uint32_t rows=0;TM_NEED(begin);if(broken){TM_NEED(tm_span(&b,at,2));at+=2;}while(rows<64){uint8_t v;TM_NEED(tm_work(&b,1)&&tm_span(&b,at,1));v=b.p[(size_t)at++];if(!v){++rows;continue;}if(v&32){uint8_t n;TM_NEED(tm_span(&b,at,2));n=b.p[(size_t)at];TM_NEED((n>=254||((n&15)<12&&(n>>4)<=7))&&b.p[(size_t)at+1]<=ni);at+=2;}if(v&64){TM_NEED(tm_span(&b,at,1)&&b.p[(size_t)at]<=64);++at;}if(v&128){TM_NEED(tm_span(&b,at,2));at+=2;}}TM_NEED(tm_claim(&b,ranges,&nr,begin,at-begin)&&tm_emit(f,s,&b,"pattern.stx",begin,at-begin));if(at>maximum)maximum=at;}
 /* Documented sample parapointers describe file bytes; alignment and appended overlays are not member data. */
 for(j=0;j<ni;++j)if(lens[j]>1){uint64_t end=(uint64_t)sample_at[j]+lens[j];TM_NEED(sample_at[j]&&tm_claim(&b,ranges,&nr,sample_at[j],lens[j])&&tm_emit(f,s,&b,"sample.pcm8",sample_at[j],lens[j]));if(end>maximum)maximum=end;}
 TM_NEED(maximum<=b.n);s->size=(int64_t)maximum;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_tracker_stx_init(xx_tracker_stx *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_STX,"tracker_stx");}}
xx_tracker_stx *xx_tracker_stx_create(xx_io_device *d,int64_t b) {xx_tracker_stx *r=(xx_tracker_stx *)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_stx_init(r,d,b);return r;}
void xx_tracker_stx_destroy(xx_tracker_stx *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_stx_free(xx_tracker_stx *r) {if(r){xx_tracker_stx_destroy(r);xx_mem_free(r);}}
bool xx_tracker_stx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tracker_stx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
