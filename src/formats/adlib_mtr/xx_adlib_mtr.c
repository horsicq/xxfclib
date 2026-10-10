/* SPDX-License-Identifier: MIT. Original music framing derived independently from primary AdPlug loader. */
#include "xxfclib/formats/adlib_mtr/xx_adlib_mtr.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 music_blob b={0};uint32_t fields[8],i,ch,np,norders,ni,restart,flen;uint64_t at=70,start,pattern_size;bool ok=false;
 static const unsigned off[8]={10,13,16,19,22,25,28,33},width[8]={2,2,2,2,2,2,4,8};
 MUSIC_NEED(music_load(f,&b,pd)&&music_tag(&b,0,"MTRACK NC ",10)&&music_span(&b,0,70)&&b.p[49]==26);for(i=0;i<8;++i){MUSIC_NEED(music_hex(&b,off[i],width[i],&fields[i]));if(i<7)MUSIC_NEED(b.p[off[i]+width[i]]==' ');}for(i=41;i<49;++i)MUSIC_NEED(b.p[i]==' ');
 ch=fields[0]+1;np=fields[2]+1;norders=fields[3]+1;ni=fields[4];restart=fields[5];flen=fields[7];MUSIC_NEED(!fields[1]&&ch&&ch<=18&&np<=256&&norders<=256&&ni&&ni<=64&&restart<norders&&fields[6]&&flen==b.n-50&&music_emit(f,s,&b,"descriptor.mtr",0,70)&&music_span(&b,at,256));
 for(i=0;i<norders;++i) { MUSIC_NEED(b.p[(size_t)at+i]<np); } MUSIC_NEED(music_emit(f,s,&b,"orders.mtr",at,256));at+=256;
 for(i=0;i<ni;++i){MUSIC_NEED(music_span(&b,at,64)&&b.p[(size_t)at+20]<=2&&music_emit(f,s,&b,"instrument.mtr",at,64));at+=64;}
 pattern_size=(uint64_t)ch*256;MUSIC_NEED(music_span(&b,at,(uint64_t)np*pattern_size));for(i=0;i<np;++i){uint64_t j;start=at;for(j=0;j<pattern_size;j+=4){uint8_t note=b.p[(size_t)(at+j)],inst=b.p[(size_t)(at+j)+1]&63;MUSIC_NEED(music_work(&b,1)&&(note&15)<=12&&inst<ni);}at+=pattern_size;MUSIC_NEED(music_emit(f,s,&b,"pattern.mtr",start,pattern_size));}
 if(at<b.n){MUSIC_NEED(b.n-at==11&&music_tag(&b,at,"[PYRO-FYRE]",11)&&music_emit(f,s,&b,"producer-tag.mtr",at,11));at+=11;}MUSIC_NEED(at==b.n);s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_mtr_init(xx_adlib_mtr *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_MTR,"adlib_mtr");}}
xx_adlib_mtr *xx_adlib_mtr_create(xx_io_device *d,int64_t b) {xx_adlib_mtr *r=(xx_adlib_mtr *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_mtr_init(r,d,b);return r;}
void xx_adlib_mtr_destroy(xx_adlib_mtr *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_mtr_free(xx_adlib_mtr *r) {if(r){xx_adlib_mtr_destroy(r);xx_mem_free(r);}}
bool xx_adlib_mtr_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_mtr_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
