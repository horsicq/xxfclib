/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/adlib_d00/xx_adlib_d00.h"
#include "../xx_fourteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 fm_blob b={0};fm_range ranges[1024];unsigned nr=0;uint64_t table,seq,inst,info,at,start,end,minseq;uint32_t songs,ns,i,j,k,maxidx=0,count,words,v=0,refs[512]={0};bool ok=false;
 FM_NEED(fm_load(f,&b,pd)&&fm_tag(&b,0,"JCH\x26\x02\x66",6)&&fm_span(&b,0,119)&&!b.p[6]&&b.p[7]==4&&b.p[8]&&b.p[9]&&!b.p[10]&&pm_le16(b.p+117)==0xffff);songs=b.p[9];table=pm_le16(b.p+107);seq=pm_le16(b.p+109);inst=pm_le16(b.p+111);info=pm_le16(b.p+113);FM_NEED(table>=119&&seq>=table+(uint64_t)songs*32&&inst>seq&&info>=inst&&info<b.n&&pm_le16(b.p+115)==seq&&(info-inst)%16==0);ns=(uint32_t)((info-inst)/16);FM_NEED(ns&&ns<=256&&fm_claim(&b,ranges,&nr,0,119,false)&&fm_claim(&b,ranges,&nr,table,(uint64_t)songs*32,false)&&fm_claim(&b,ranges,&nr,inst,(uint64_t)ns*16,false)&&fm_claim(&b,ranges,&nr,info,b.n-info,false)&&fm_emit(f,s,&b,"descriptor.d00",0,119)&&fm_emit(f,s,&b,"subsongs.d00",table,(uint64_t)songs*32)&&fm_emit(f,s,&b,"instruments.d00",inst,(uint64_t)ns*16)&&fm_emit(f,s,&b,"data-info.d00",info,b.n-info));FM_NEED(b.n-info==2&&b.p[(size_t)b.n-2]==255&&b.p[(size_t)b.n-1]==255);
 for(i=0;i<songs;++i)for(j=0;j<9;++j){start=pm_le16(b.p+(size_t)table+i*32+j*2);if(!start)continue;FM_NEED(start>=119&&start+4<=table);at=start+2;words=0;while(at+2<=table){FM_NEED(fm_work(&b,1));v=pm_le16(b.p+(size_t)at);at+=2;++words;if(v==65534)break;if(v==65535){FM_NEED(at+2<=table&&pm_le16(b.p+(size_t)at)<words-1);at+=2;break;}if(v<0x8000){FM_NEED(v<512);if(v>maxidx)maxidx=v;}else FM_NEED(v<0xa000);}FM_NEED(v==65534||v==65535);FM_NEED(fm_claim(&b,ranges,&nr,start,at-start,true)&&fm_emit(f,s,&b,"orders.d00",start,at-start));}
 count=maxidx+1;FM_NEED(fm_span(&b,seq,(uint64_t)count*2)&&seq+(uint64_t)count*2<=inst&&fm_claim(&b,ranges,&nr,seq,(uint64_t)count*2,false)&&fm_emit(f,s,&b,"sequence-index.d00",seq,(uint64_t)count*2));minseq=inst;
 for(i=0;i<count;++i){refs[i]=pm_le16(b.p+(size_t)seq+i*2);FM_NEED(refs[i]>=seq+(uint64_t)count*2&&refs[i]<inst);if(refs[i]<minseq)minseq=refs[i];}
 FM_NEED(minseq==seq+(uint64_t)count*2);for(i=0;i<count;++i){start=refs[i];at=start;end=inst;for(k=0;k<count;++k)if(refs[k]>start&&refs[k]<end)end=refs[k];FM_NEED(!((end-start)&1));while(at<end){FM_NEED(at+2<=end&&fm_work(&b,1));v=pm_le16(b.p+(size_t)at);at+=2;if(v==65535)break;if((v>>12)==12)FM_NEED((v&4095)<ns);FM_NEED((v>>12)!=11);}FM_NEED(v==65535&&at==end&&fm_claim(&b,ranges,&nr,start,end-start,true)&&fm_emit(f,s,&b,"sequence.d00",start,end-start));}
 s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_d00_init(xx_adlib_d00 *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_D00,"adlib_d00");}}
xx_adlib_d00 *xx_adlib_d00_create(xx_io_device *d,int64_t b) {xx_adlib_d00 *r=(xx_adlib_d00 *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_d00_init(r,d,b);return r;}
void xx_adlib_d00_destroy(xx_adlib_d00 *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_d00_free(xx_adlib_d00 *r) {if(r){xx_adlib_d00_destroy(r);xx_mem_free(r);}}
bool xx_adlib_d00_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_d00_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
