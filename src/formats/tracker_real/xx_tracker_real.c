/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/tracker_real/xx_tracker_real.h"
#include "../xx_fourteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 fm_blob b={0};uint64_t at=0,head,packed,start,q,end;uint32_t ch,ni,no,np,ver,i,j,k,rows,nc,ns,sz;bool ok=false;
 FM_NEED(fm_load(f,&b,pd)&&fm_tag(&b,0,"RTMM ",5)&&fm_span(&b,0,140)&&b.p[37]==26);ver=pm_le16(b.p+38);head=pm_le16(b.p+40);FM_NEED((ver==0x110||ver==0x112)&&head==(ver==0x112?130U:98U)&&fm_span(&b,42,head));ch=b.p[96];ni=b.p[97];no=pm_le16(b.p+98);np=pm_le16(b.p+100);packed=pm_le32(b.p+136);FM_NEED(ch&&ch<=32&&ni<=128&&no&&no<=255&&np&&np<=255&&!(pm_le16(b.p+94)&~3U)&&b.p[102]&&b.p[103]);at=42+head;FM_NEED(fm_span(&b,at,packed)&&packed>=(uint64_t)no*2);for(i=0;i<no;++i)FM_NEED(pm_le16(b.p+(size_t)at+i*2)<np);FM_NEED(fm_emit(f,s,&b,"descriptor.rtm",0,at)&&fm_emit(f,s,&b,"extra-data.rtm",at,packed));at+=packed;
 for(i=0;i<np;++i){start=at;FM_NEED(fm_tag(&b,at,"RTND ",5)&&fm_span(&b,at,51)&&b.p[(size_t)at+37]==26&&pm_le16(b.p+(size_t)at+38)==ver&&pm_le16(b.p+(size_t)at+40)==9);FM_NEED(pm_le16(b.p+(size_t)at+42)==1);nc=b.p[(size_t)at+44];rows=pm_le16(b.p+(size_t)at+45);sz=pm_le32(b.p+(size_t)at+47);FM_NEED(nc&&nc<=ch&&rows&&rows<=999);at+=51;FM_NEED(fm_span(&b,at,sz));end=at+sz;
  for(j=0;j<rows;++j){uint32_t channel=0;for(;;){uint8_t c;FM_NEED(at<end&&fm_work(&b,1));c=b.p[(size_t)at++];if(!c)break;FM_NEED(!(c&128));if(c&1){FM_NEED(at<end);channel=b.p[(size_t)at++];}FM_NEED(channel<nc);for(k=1;k<7;++k)if(c&(1U<<k)){FM_NEED(at<end);if(k==2)FM_NEED(b.p[(size_t)at]<=ni);++at;}++channel;}}FM_NEED(at==end&&fm_emit(f,s,&b,"pattern-object.rtm",start,end-start));
 }
 for(i=0;i<ni;++i){start=at;FM_NEED(fm_tag(&b,at,"RTIN ",5)&&fm_span(&b,at,42)&&b.p[(size_t)at+37]==26&&pm_le16(b.p+(size_t)at+38)==ver);head=pm_le16(b.p+(size_t)at+40);at+=42;if(!head){FM_NEED(fm_emit(f,s,&b,"empty-instrument.rtm",start,42));continue;}FM_NEED(head==(ver==0x112?341U:337U)&&fm_span(&b,at,head));ns=b.p[(size_t)at];FM_NEED(ns<=16&&!(pm_le16(b.p+(size_t)at+1)&~3U));for(j=0;j<120;++j)FM_NEED(!ns||b.p[(size_t)at+3+j]<ns);
  for(j=0;j<2;++j){q=at+123+(uint64_t)j*102;nc=b.p[(size_t)q];FM_NEED(nc<=12&&!(pm_le16(b.p+(size_t)q+100)&~7U));for(k=0;k<nc;++k){uint32_t x=pm_le32(b.p+(size_t)q+1+k*8);FM_NEED(x<=2147483647U&&(k==0||x>=pm_le32(b.p+(size_t)q+1+(k-1)*8)));}FM_NEED(!nc||(b.p[(size_t)q+97]<nc&&b.p[(size_t)q+98]<nc&&b.p[(size_t)q+99]<nc));}FM_NEED(fm_emit(f,s,&b,"instrument-object.rtm",start,42+head));at+=head;
  for(j=0;j<ns;++j){uint32_t flags,lp,lz;start=at;FM_NEED(fm_tag(&b,at,"RTSM ",5)&&fm_span(&b,at,68)&&b.p[(size_t)at+37]==26&&pm_le16(b.p+(size_t)at+38)==ver&&pm_le16(b.p+(size_t)at+40)==26);flags=pm_le16(b.p+(size_t)at+42);sz=pm_le32(b.p+(size_t)at+46);lp=pm_le32(b.p+(size_t)at+54);lz=pm_le32(b.p+(size_t)at+58);FM_NEED(!(flags&~6U)&&b.p[(size_t)at+44]<=64&&b.p[(size_t)at+45]<=64&&lp<=lz&&lz<=sz&&(!(flags&2)||!(sz&1)));at+=68;FM_NEED(fm_span(&b,at,sz)&&fm_emit(f,s,&b,"sample-object.rtm",start,68+(uint64_t)sz));at+=sz;}
 }
 FM_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_tracker_real_init(xx_tracker_real *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_REAL,"tracker_real");}}
xx_tracker_real *xx_tracker_real_create(xx_io_device *d,int64_t b) {xx_tracker_real *r=(xx_tracker_real *)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_real_init(r,d,b);return r;}
void xx_tracker_real_destroy(xx_tracker_real *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_real_free(xx_tracker_real *r) {if(r){xx_tracker_real_destroy(r);xx_mem_free(r);}}
bool xx_tracker_real_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tracker_real_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
