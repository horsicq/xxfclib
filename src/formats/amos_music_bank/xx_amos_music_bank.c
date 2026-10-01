/* SPDX-License-Identifier: MIT. Original validated encoded components; no playback. */
#include "xxfclib/formats/amos_music_bank/xx_amos_music_bank.h"
#include "../xx_fourteenth_media.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {

 fm_blob b={0};fm_range ranges[1024];unsigned nr=0;uint64_t inst,song,pat,record,at,a,end,max=0;uint32_t ni,np,i,j,n,lp;uint32_t offsets[1024],unique=0;bool ok=false;
 FM_NEED(fm_load(f,&b,pd)&&fm_span(&b,0,32)&&fm_tag(&b,0,"AmBk",4)&&pm_be16(b.p+4)==3&&fm_tag(&b,12,"Music   ",8)&&(pm_be32(b.p+8)&0x7fffffffU)==b.n-12);inst=20+(uint64_t)pm_be32(b.p+20);song=20+(uint64_t)pm_be32(b.p+24);pat=20+(uint64_t)pm_be32(b.p+28);FM_NEED(inst<song&&song<pat&&fm_claim(&b,ranges,&nr,0,32,false)&&fm_span(&b,inst,2)&&fm_span(&b,song,6)&&fm_span(&b,pat,2));ni=pm_be16(b.p+(size_t)inst);np=pm_be16(b.p+(size_t)pat);FM_NEED(ni&&ni<=255&&np&&np<=256&&pm_be16(b.p+(size_t)song)==1&&fm_claim(&b,ranges,&nr,inst,2+(uint64_t)ni*32,false)&&fm_claim(&b,ranges,&nr,song,6,false)&&fm_claim(&b,ranges,&nr,pat,2+(uint64_t)np*8,false));record=song+pm_be32(b.p+(size_t)song+2);FM_NEED(fm_claim(&b,ranges,&nr,record,28,false)&&pm_be16(b.p+(size_t)record+8)>0);
 FM_NEED(fm_emit(f,s,&b,"descriptor.abk",0,32)&&fm_emit(f,s,&b,"instruments.abk",inst,2+(uint64_t)ni*32)&&fm_emit(f,s,&b,"song-index.abk",song,6)&&fm_emit(f,s,&b,"song.abk",record,28));
 for(i=0;i<4;++i){a=record+pm_be16(b.p+(size_t)record+i*2);at=a;n=0;for(;;){uint16_t v;FM_NEED(fm_span(&b,at,2)&&fm_work(&b,1)&&n<=1024);v=pm_be16(b.p+(size_t)at);at+=2;if(v==0xffff||v==0xfffe)break;FM_NEED(v<np);++n;}FM_NEED(fm_claim(&b,ranges,&nr,a,at-a,true)&&fm_emit(f,s,&b,"playlist.abk",a,at-a));}
 FM_NEED(fm_emit(f,s,&b,"pattern-index.abk",pat,2+(uint64_t)np*8));for(i=0;i<np*4;++i){uint32_t v=pm_be16(b.p+(size_t)pat+2+i*2);bool found=false;FM_NEED(v>=2+np*8&&pat+v<b.n);for(j=0;j<unique;++j)if(offsets[j]==v)found=true;if(!found){FM_NEED(unique<1024);offsets[unique++]=v;}}
 for(i=0;i<unique;++i){uint32_t pos=0;bool terminated=false;a=pat+offsets[i];end=b.n;for(j=0;j<unique;++j)if(offsets[j]>offsets[i]&&pat+offsets[j]<end)end=pat+offsets[j];at=a;while(at<end){uint16_t v;FM_NEED(fm_span(&b,at,2)&&fm_work(&b,1));v=pm_be16(b.p+(size_t)at);at+=2;if(v==0x8000||v==0x9100){FM_NEED(at==end);terminated=true;break;}if(v&0x8000){uint32_t cmd=(v>>8)&127,param=v&127;FM_NEED(cmd>=1&&cmd<=17);if(cmd==9)FM_NEED(param<ni);if(cmd==16)pos+=param;}else if(v&0x4000){FM_NEED(at+2<=end);at+=2;pos+=v&255;}FM_NEED(pos<=256);}FM_NEED(terminated&&at==end&&fm_claim(&b,ranges,&nr,a,end-a,false)&&fm_emit(f,s,&b,"pattern-track.abk",a,end-a));}
 for(i=0;i<ni;++i){const uint8_t *q=b.p+(size_t)inst+2+i*32;uint32_t z=pm_be16(q+14);n=z>4?z:pm_be16(q+8);lp=pm_be32(q+4);a=inst+pm_be32(q);FM_NEED(pm_be16(q+12)<=64);if(n){FM_NEED(a>=inst+2+(uint64_t)ni*32&&a+(uint64_t)n*2<=song&&(lp<=pm_be32(q)||lp-pm_be32(q)<=(uint64_t)n)&&fm_claim(&b,ranges,&nr,a,(uint64_t)n*2,true)&&fm_emit(f,s,&b,"sample.pcm8",a,(uint64_t)n*2));}}
 for(i=0;i<nr;++i)if(ranges[i].at+ranges[i].n>max)max=ranges[i].at+ranges[i].n;FM_NEED(max==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_amos_music_bank_init(xx_amos_music_bank *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AMOS_MUSIC_BANK,"amos_music_bank");}}
xx_amos_music_bank *xx_amos_music_bank_create(xx_io_device *d,int64_t b) {xx_amos_music_bank *r=(xx_amos_music_bank *)xx_mem_alloc(sizeof(*r));if(r)xx_amos_music_bank_init(r,d,b);return r;}
void xx_amos_music_bank_destroy(xx_amos_music_bank *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_amos_music_bank_free(xx_amos_music_bank *r) {if(r){xx_amos_music_bank_destroy(r);xx_mem_free(r);}}
bool xx_amos_music_bank_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_amos_music_bank_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
