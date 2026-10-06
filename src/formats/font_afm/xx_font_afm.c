/* SPDX-License-Identifier: MIT
 * Primary reference: https://adobe-type-tools.github.io/font-tech-notes/pdfs/5004.AFM_Spec.pdf
 * Adobe AFM3/4.1 complete bounded font/global metrics, character metric and optional kerning/track/composite sections. Finite metrics and local glyph references validated; original metrics exported; AMFM/MM and font execution unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/font_afm/xx_font_afm.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[16];return n>=16&&pm_read(f,0,b,16)&&tb_tag(b,"StartFontMetrics",16);}
typedef struct af_state {char (*names)[128];char (*refs)[128];uint32_t refs_count,work;xx_pd_struct *pd;} af_state;
static bool af_name(tb_text *q,char out[128]) {unsigned n=0;tb_space(q);while(q->t<q->stop&&q->b[q->t]!=32&&q->b[q->t]!=9){uint8_t c=q->b[q->t++];if(c<33||c>126||c==';'||n==127)return false;out[n++]=(char)c;}out[n]=0;return n!=0;}
static bool af_lookup(af_state *a,const char *name,bool insert) {uint32_t hash=2166136261U,j;const unsigned char *p=(const unsigned char *)name;while(*p)hash=(hash^*p++)*16777619U;
 for(j=0;j<8192;++j){uint32_t at=(hash+j)&8191U;if(++a->work>16000000||tb_stop(a->pd))return false;if(!a->names[at][0]){if(!insert)return false;xx_rt_snprintf(a->names[at],128,"%s",name);return true;}if(!xx_rt_strcmp(a->names[at],name))return !insert;}return false;
}
static bool af_nums(tb_text *q,unsigned n) {return tb_nums(q,n);}
static bool af_char(af_state *a,tb_text *q) {
 uint64_t end=q->stop;uint32_t flags=0;char name[128];bool width=false;
 while(q->t<end){uint64_t semi=q->t;double box[4];int32_t code;unsigned i;while(semi<end&&q->b[semi]!=';')++semi;if(semi==end)return false;q->stop=semi;
  if(tb_word(q,"C")){if((flags&1)||!tb_i(q,&code)||code< -1||code>65535||!tb_done(q))return false;flags|=1;}
  else if(tb_word(q,"CH")){unsigned digits=0;uint32_t v=0;if(flags&1)return false;tb_space(q);if(q->t==q->stop||q->b[q->t++]!='<')return false;while(q->t<q->stop&&q->b[q->t]!='>'){uint8_t c=q->b[q->t++];unsigned d=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:16;if(d>15||++digits>4)return false;v=v*16+d;}if(!digits||q->t==q->stop||q->b[q->t++]!='>'||!tb_done(q))return false;flags|=1;(void)v;}
  else if(tb_word(q,"N")){if((flags&2)||!af_name(q,name)||!tb_done(q)||!af_lookup(a,name,true))return false;flags|=2;}
  else if(tb_word(q,"B")){if(flags&4)return false;for(i=0;i<4;++i)if(!tb_num(q,&box[i]))return false;if(box[0]>box[2]||box[1]>box[3]||!tb_done(q))return false;flags|=4;}
  else if(tb_word(q,"WX")||tb_word(q,"WY")||tb_word(q,"W0X")||tb_word(q,"W0Y")||tb_word(q,"W1X")||tb_word(q,"W1Y")){if(!af_nums(q,1))return false;width=true;}
  else if(tb_word(q,"W")||tb_word(q,"W0")||tb_word(q,"W1")||tb_word(q,"VV")){if(!af_nums(q,2))return false;width=true;}
  else if(tb_word(q,"L")){if(a->refs_count>65533||!af_name(q,a->refs[a->refs_count++])||!af_name(q,a->refs[a->refs_count++])||!tb_done(q))return false;}
  else { return false; } q->t=semi+1;q->stop=end;tb_space(q);
 }return flags==7&&width;
}
static bool af_global(tb_text *q) {
 const char *strings[]={"FontName","FullName","FamilyName","Weight","Notice","Version","EncodingScheme","CharacterSet","Comment"};const char *numbers[]={"ItalicAngle","UnderlinePosition","UnderlineThickness","CapHeight","XHeight","Ascender","Descender","Characters","StdHW","StdVW"};
 unsigned i;for(i=0;i<sizeof(strings)/sizeof(strings[0]);++i)if(tb_word(q,strings[i])){tb_space(q);return q->t<q->stop&&q->stop-q->t<4096;}
 for(i=0;i<sizeof(numbers)/sizeof(numbers[0]);++i)if(tb_word(q,numbers[i]))return af_nums(q,1);
 if(tb_word(q,"FontBBox")){double x[4];for(i=0;i<4;++i)if(!tb_num(q,&x[i]))return false;return x[0]<=x[2]&&x[1]<=x[3]&&tb_done(q);}
 if(tb_word(q,"IsFixedPitch")||tb_word(q,"IsBaseFont"))return (tb_word(q,"true")||tb_word(q,"false"))&&tb_done(q);
 if(tb_word(q,"MetricsSets")){int32_t v;return tb_i(q,&v)&&v==0&&tb_done(q);}return false;
}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tb_text q={b,0,n,0,0,0};af_state a;bool result=false,characters=false,fontname=false,kern=false,composite=false,end=false;uint64_t start;int32_t count,i;unsigned j;char name[128],other[128];xx_mem_zero(&a,sizeof(a));a.pd=pd;
 if(!tb_utf(b,n,false,pd)||!tb_line(&q)||!tb_word(&q,"StartFontMetrics")||(!tb_word(&q,"3.0")&&!tb_word(&q,"4.1"))||!tb_done(&q))return false;
 a.names=(char (*)[128])xx_mem_alloc(8192*128);a.refs=(char (*)[128])xx_mem_alloc(65536*128);if(!a.names||!a.refs)goto done;xx_mem_zero(a.names,8192*128);
#define AF(x) do{if(!(x))goto done;}while(0)
 while(q.p<n){AF(!tb_stop(pd)&&tb_line(&q));if(tb_done(&q))continue;
  if(tb_word(&q,"StartCharMetrics")){AF(!characters&&fontname&&!kern&&!composite&&tb_i(&q,&count)&&count>0&&count<=4096&&tb_done(&q)&&tb_emit(f,s,"descriptor.afm",0,q.p,n));characters=true;start=q.p;
   for(i=0;i<count;++i) {AF(tb_line(&q)&&af_char(&a,&q)); } AF(tb_line(&q)&&tb_word(&q,"EndCharMetrics")&&tb_done(&q)&&tb_emit(f,s,"character-metrics.afm",start,q.p-start,n));
   for(j=0;j<a.refs_count;++j)AF(af_lookup(&a,a.refs[j],false));
  }
  else if(tb_word(&q,"StartKernData")){AF(characters&&!kern&&!composite&&tb_done(&q));kern=true;start=q.start;
   for(;;){AF(tb_line(&q));if(tb_word(&q,"EndKernData")){AF(tb_done(&q));break;}
    if(tb_word(&q,"StartKernPairs")||tb_word(&q,"StartKernPairs0")||tb_word(&q,"StartKernPairs1")){
     AF(tb_i(&q,&count)&&count>=0&&count<=1000000&&tb_done(&q));
     for(i=0;i<count;++i){unsigned nums;AF(tb_line(&q));if(tb_word(&q,"KPX")||tb_word(&q,"KPY"))nums=1;else if(tb_word(&q,"KP"))nums=2;else goto done;AF(af_name(&q,name)&&af_name(&q,other)&&af_lookup(&a,name,false)&&af_lookup(&a,other,false)&&af_nums(&q,nums));}
     AF(tb_line(&q)&&tb_word(&q,"EndKernPairs")&&tb_done(&q));
    }else if(tb_word(&q,"StartTrackKern")){AF(tb_i(&q,&count)&&count>=0&&count<=4096&&tb_done(&q));for(i=0;i<count;++i){int32_t degree;double x[4];AF(tb_line(&q)&&tb_word(&q,"TrackKern")&&tb_i(&q,&degree));for(j=0;j<4;++j)AF(tb_num(&q,&x[j]));AF(x[0]<=x[2]&&tb_done(&q));}AF(tb_line(&q)&&tb_word(&q,"EndTrackKern")&&tb_done(&q));}
    else goto done;
   }AF(tb_emit(f,s,"kerning.afm",start,q.p-start,n));
  }
  else if(tb_word(&q,"StartComposites")){AF(characters&&!composite&&tb_i(&q,&count)&&count>=0&&count<=4096&&tb_done(&q));composite=true;start=q.start;
   for(i=0;i<count;++i){int32_t parts,k;uint64_t lineend;AF(tb_line(&q)&&tb_word(&q,"CC")&&af_name(&q,name)&&af_lookup(&a,name,false)&&tb_i(&q,&parts)&&parts>0&&parts<=1024);tb_space(&q);AF(q.t<q.stop&&b[q.t++]==';');lineend=q.stop;
    for(k=0;k<parts;++k){uint64_t semi=q.t;while(semi<lineend&&b[semi]!=';')++semi;AF(semi<lineend);q.stop=semi;AF(tb_word(&q,"PCC")&&af_name(&q,name)&&af_lookup(&a,name,false)&&af_nums(&q,2));q.t=semi+1;q.stop=lineend;}AF(tb_done(&q));
   }AF(tb_line(&q)&&tb_word(&q,"EndComposites")&&tb_done(&q)&&tb_emit(f,s,"composites.afm",start,q.p-start,n));
  }
  else if(tb_word(&q,"EndFontMetrics")){AF(characters&&tb_done(&q));start=q.start;while(q.p<n)AF(tb_line(&q)&&tb_done(&q));AF(tb_emit(f,s,"terminator.afm",start,n-start,n));end=true;break;}
  else if(tb_word(&q,"FontName")){AF(!characters&&!fontname&&af_name(&q,name)&&tb_done(&q));fontname=true;}
  else AF(!characters&&af_global(&q));
 }AF(end);if(!tb_cover(f,s,"comments.afm",n))goto done;s->size=(int64_t)n;result=true;
done:xx_mem_free(a.names);xx_mem_free(a.refs);return result;
#undef AF
}

void xx_font_afm_init(xx_font_afm *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_FONT_AFM,"afm");}}
xx_font_afm *xx_font_afm_create(xx_io_device *d,int64_t at) {xx_font_afm *r=(xx_font_afm *)xx_mem_alloc(sizeof(*r));if(r)xx_font_afm_init(r,d,at);return r;}
void xx_font_afm_destroy(xx_font_afm *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_font_afm_free(xx_font_afm *r) {if(r){xx_font_afm_destroy(r);xx_mem_free(r);}}
bool xx_font_afm_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_font_afm_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
