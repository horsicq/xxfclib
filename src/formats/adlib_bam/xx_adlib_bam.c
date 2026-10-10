/* SPDX-License-Identifier: MIT. Validated original encoded music components; no playback. */
#include "xxfclib/formats/adlib_bam/xx_adlib_bam.h"
#include "../common/xx_music_components.h"
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 music_blob b={0};uint64_t at=4,run=4;uint16_t labels=1,refs=0;bool end=false,ok=false;
 MUSIC_NEED(music_load(f,&b,pd)&&music_tag(&b,0,"CBMF",4)&&music_emit(f,s,&b,"descriptor.bam",0,4));
 while(at<b.n){uint64_t start=at;uint8_t c,op,ch;MUSIC_NEED(music_work(&b,1));c=b.p[(size_t)at++];op=c&240;ch=c&15;
  if(c>=128)continue;
  if(op==0){MUSIC_NEED(at==b.n);end=true;break;}
  if(op==16){MUSIC_NEED(ch<9&&music_span(&b,at,1)&&b.p[(size_t)at]<128);++at;}
  else if(op==32)MUSIC_NEED(ch<9);
  else if(op==48){MUSIC_NEED(ch<9&&music_span(&b,at,11));if(start>run)MUSIC_NEED(music_emit(f,s,&b,"commands.bam",run,start-run));MUSIC_NEED(music_emit(f,s,&b,"instrument.bam",start,12));at+=11;run=at;}
  else if(op==80)labels|=(uint16_t)(1U<<ch);
  else if(op==96){MUSIC_NEED(music_span(&b,at,1)&&(labels&(uint16_t)(1U<<ch)));refs|=(uint16_t)(1U<<ch);++at;}
  else if(op==112)MUSIC_NEED(ch==0);
  else MUSIC_NEED(false);
 }
 MUSIC_NEED(end&&!(refs&~labels));if(at>run)MUSIC_NEED(music_emit(f,s,&b,"commands.bam",run,at-run));s->size=(int64_t)b.n;ok=true;
done:xx_mem_free(b.p);return ok;
}
void xx_adlib_bam_init(xx_adlib_bam *r,xx_io_device *d,int64_t b) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_ADLIB_BAM,"adlib_bam");}}
xx_adlib_bam *xx_adlib_bam_create(xx_io_device *d,int64_t b) {xx_adlib_bam *r=(xx_adlib_bam *)xx_mem_alloc(sizeof(*r));if(r)xx_adlib_bam_init(r,d,b);return r;}
void xx_adlib_bam_destroy(xx_adlib_bam *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_adlib_bam_free(xx_adlib_bam *r) {if(r){xx_adlib_bam_destroy(r);xx_mem_free(r);}}
bool xx_adlib_bam_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_adlib_bam_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
