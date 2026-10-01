/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/tracker_megatracker/xx_tracker_megatracker.h"
#include "../xx_fourteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 fm_blob b={0};fm_range ranges[1024];unsigned nr=0;uint64_t song,seq,inst,pat,trk,end=0,at,a;uint32_t ch,ns,np,nt,ni,no,i,j,rows;bool ok=false;
 FM_NEED(fm_load(f,&b,pd)&&fm_span(&b,0,58)&&fm_tag(&b,0,"MGT",3)&&fm_tag(&b,4,"\xbdMCS",4)&&b.p[3]==0x11);ch=pm_be16(b.p+8);ns=pm_be16(b.p+10);np=pm_be16(b.p+14);nt=pm_be16(b.p+16);ni=pm_be16(b.p+18);song=pm_be32(b.p+26);inst=pm_be32(b.p+34);pat=pm_be32(b.p+38);trk=pm_be32(b.p+42);
 FM_NEED(ch&&ch<=32&&ns==1&&np&&np<=256&&nt&&nt<=512&&ni&&ni<=64&&fm_claim(&b,ranges,&nr,0,58,false)&&fm_claim(&b,ranges,&nr,song,46+(uint64_t)ch*2,false));seq=pm_be32(b.p+(size_t)song+32);no=pm_be16(b.p+(size_t)song+36);FM_NEED(no&&no<=256&&pm_be16(b.p+(size_t)song+38)<no&&b.p[(size_t)song+40]&&b.p[(size_t)song+41]&&fm_claim(&b,ranges,&nr,seq,(uint64_t)no*2,false));
 FM_NEED(fm_emit(f,s,&b,"descriptor.mgt",0,58)&&fm_emit(f,s,&b,"song.mgt",song,46+(uint64_t)ch*2)&&fm_emit(f,s,&b,"orders.mgt",seq,(uint64_t)no*2));for(i=0;i<no;++i)FM_NEED(pm_be16(b.p+(size_t)seq+i*2)<np);
 FM_NEED(fm_claim(&b,ranges,&nr,inst,(uint64_t)ni*80,false)&&fm_emit(f,s,&b,"instruments.mgt",inst,(uint64_t)ni*80)&&fm_claim(&b,ranges,&nr,pat,(uint64_t)np*(2+ch*2),false)&&fm_emit(f,s,&b,"pattern-map.mgt",pat,(uint64_t)np*(2+ch*2))&&fm_claim(&b,ranges,&nr,trk,(uint64_t)nt*4,false)&&fm_emit(f,s,&b,"track-offsets.mgt",trk,(uint64_t)nt*4));
 for(i=0;i<np;++i){const uint8_t *q=b.p+(size_t)pat+i*(2+ch*2);FM_NEED(pm_be16(q)>0&&pm_be16(q)<=256);for(j=0;j<ch;++j)FM_NEED(pm_be16(q+2+j*2)>0&&pm_be16(q+2+j*2)<=nt);}
 for(i=1;i<nt;++i){uint32_t row=0;a=pm_be32(b.p+(size_t)trk+i*4);at=a;FM_NEED(fm_span(&b,at,2));rows=pm_be16(b.p+(size_t)at);at+=2;FM_NEED(rows&&rows<=255);while(row<rows){uint8_t v;FM_NEED(fm_work(&b,1)&&fm_span(&b,at,1));v=b.p[(size_t)at++];row+=v&3;FM_NEED(row<rows);for(j=2;j<8;++j)if(v&(1U<<j)){FM_NEED(fm_span(&b,at,1));if(j==3)FM_NEED(b.p[(size_t)at]<=ni);++at;}++row;}FM_NEED(fm_claim(&b,ranges,&nr,a,at-a,true)&&fm_emit(f,s,&b,"track.mgt",a,at-a));}
 for(i=0;i<ni;++i){const uint8_t *q=b.p+(size_t)inst+i*80;uint64_t n=pm_be32(q+36),lp=pm_be32(q+40),lz=pm_be32(q+44);FM_NEED(lp<=n&&lz<=n-lp&&!(q[64]&~3U));if(n){a=pm_be32(q+32);FM_NEED(fm_claim(&b,ranges,&nr,a,n,true)&&fm_emit(f,s,&b,"sample.pcm8",a,n));}}
 for(i=0;i<nr;++i)if(ranges[i].at+ranges[i].n>end)end=ranges[i].at+ranges[i].n;FM_NEED(end==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_tracker_megatracker_init(xx_tracker_megatracker *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_TRACKER_MEGATRACKER,"tracker_megatracker");}}
xx_tracker_megatracker *xx_tracker_megatracker_create(xx_io_device *d,int64_t b) {xx_tracker_megatracker *r=(xx_tracker_megatracker *)xx_mem_alloc(sizeof(*r));if(r)xx_tracker_megatracker_init(r,d,b);return r;}
void xx_tracker_megatracker_destroy(xx_tracker_megatracker *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tracker_megatracker_free(xx_tracker_megatracker *r) {if(r){xx_tracker_megatracker_destroy(r);xx_mem_free(r);}}
bool xx_tracker_megatracker_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tracker_megatracker_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
