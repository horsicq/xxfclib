/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/stepcode/stepcode/develop/src/clstepcore/read_func.cc
 * STEP Part21 cleartext edition1/2 framing: mandatory typed HEADER records, complete DATA entities/recursive parameters, unique positive IDs and resolved local references. Original encoded descriptor/entity records exported. Schema-specific CAD evaluation, SCOPE, ANCHOR/REFERENCE/signature sections and encoded-string directives declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/step_part21/xx_step_part21.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[13];return n>=24&&pm_read(f,0,b,13)&&tg_tag(b,"ISO-10303-21;",13);}
typedef struct step_state {tg_lex q;tg_ids ids;uint32_t *refs,refcount,refcap;} step_state;
static bool step_ref(step_state *v,uint32_t id) {uint32_t cap;void *p;if(v->refcount==v->refcap){cap=v->refcap?v->refcap*2:64;if(cap>262144)return false;p=xx_mem_realloc(v->refs,(size_t)cap*4);if(!p)return false;v->refs=(uint32_t *)p;v->refcap=cap;}v->refs[v->refcount++]=id;return true;}
static bool step_value(step_state *,unsigned,bool);
static bool step_params(step_state *v,unsigned depth,bool header) {unsigned count=0;if(depth>32||!tg_char(&v->q,'('))return false;if(tg_char(&v->q,')'))return true;do {if(++count>100000||!step_value(v,depth+1,header))return false;}while(tg_char(&v->q,','));return tg_char(&v->q,')');}
static bool step_value(step_state *v,unsigned depth,bool header) {
 tg_lex *q=&v->q;double number;int32_t id;uint64_t p,z;
 if(depth>32||!tg_skip(q)||q->p==q->n)return false;
 if(q->b[q->p]=='(')return step_params(v,depth,header);
 if(q->b[q->p]=='\'')return tg_quoted(q,'\'',NULL,NULL);
 if(tg_char(q,'$')||tg_char(q,'*'))return !header;
 if(tg_char(q,'#'))return !header&&tg_integer(q,&id)&&id>0&&step_ref(v,(uint32_t)id);
 if(q->b[q->p]=='.'&&tg_span(q->p,2,q->n)&&((q->b[q->p+1]>='A'&&q->b[q->p+1]<='Z')||(q->b[q->p+1]>='a'&&q->b[q->p+1]<='z'))){++q->p;return tg_ident(q,&p,&z)&&tg_char(q,'.');}
 if(q->b[q->p]=='"'){unsigned used=0;uint8_t c;++q->p;if(q->p==q->n||(c=q->b[q->p++])<'0'||c>'3')return false;while(q->p<q->n&&q->b[q->p]!='"'){c=q->b[q->p++];if(!((c>='0'&&c<='9')||(c>='A'&&c<='F'))||++used>8192)return false;}return used&&tg_char(q,'"');}
 if((q->b[q->p]>='A'&&q->b[q->p]<='Z')||(q->b[q->p]>='a'&&q->b[q->p]<='z'))return !header&&tg_ident(q,&p,&z)&&step_params(v,depth+1,false);
 return tg_number(q,&number);
}
static bool step_string_list(tg_lex *q) {unsigned count=0;if(!tg_char(q,'('))return false;do {if(++count>1024||!tg_quoted(q,'\'',NULL,NULL))return false;}while(tg_char(q,','));return tg_char(q,')');}
static bool step_header(step_state *v,const char *name,unsigned count) {unsigned i;tg_lex *q=&v->q;if(!tg_kw(q,name)||!tg_char(q,'('))return false;for(i=0;i<count;++i){bool list=count==1||(count==2&&i==0)||(count==7&&(i==2||i==3));if(i&&!tg_char(q,','))return false;if(list){if(!step_string_list(q))return false;}else if(!tg_quoted(q,'\'',NULL,NULL))return false;}return tg_char(q,')')&&tg_char(q,';');}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 step_state v={{b,0,n,pd,0,false,false,true},{0},NULL,0,0};tg_lex *q=&v.q;bool ok=false;uint32_t count=0,i;char label[48];
 if(!tg_utf(b,n,true,pd)||!tg_ids_init(&v.ids,4096)||!tg_kw(q,"ISO-10303-21")||!tg_char(q,';')||!tg_kw(q,"HEADER")||!tg_char(q,';')||!step_header(&v,"FILE_DESCRIPTION",2)||!step_header(&v,"FILE_NAME",7)||!step_header(&v,"FILE_SCHEMA",1)||!tg_kw(q,"ENDSEC")||!tg_char(q,';')||!tg_kw(q,"DATA")||!tg_char(q,';')||!tg_emit(f,s,"descriptor.step",0,q->p,n))goto done;
 while(!tg_kw(q,"ENDSEC")){int32_t id;uint64_t start=q->p,at,z;
  if(++count>4093||!tg_char(q,'#')||!tg_integer(q,&id)||id<1||!tg_id(&v.ids,(uint32_t)id,true,pd)||!tg_char(q,'='))goto done;
  if(tg_char(q,'(')){unsigned types=0;do {if(++types>1024||!tg_ident(q,&at,&z)||!step_params(&v,0,false))goto done;}while(!tg_char(q,')'));}
  else if(!tg_ident(q,&at,&z)||!step_params(&v,0,false))goto done;
  if(!tg_char(q,';'))goto done;xx_rt_snprintf(label,sizeof(label),"entity-%u.step",(unsigned)id);if(!tg_emit(f,s,label,start,q->p-start,n))goto done;
 }
 if(!count||!tg_char(q,';')||!tg_kw(q,"END-ISO-10303-21")||!tg_char(q,';')||!tg_end(q))goto done;
 for(i=0;i<v.refcount;++i)if(!tg_id(&v.ids,v.refs[i],false,pd))goto done;
 if(!tg_cover(f,s,"syntax.step",n))goto done;s->size=(int64_t)n;ok=true;
done:if(v.ids.values)xx_mem_free(v.ids.values);if(v.refs)xx_mem_free(v.refs);return ok;
}

void xx_step_part21_init(xx_step_part21 *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_STEP_PART21,"step");}}
xx_step_part21 *xx_step_part21_create(xx_io_device *d,int64_t at) {xx_step_part21 *r=(xx_step_part21 *)xx_mem_alloc(sizeof(*r));if(r)xx_step_part21_init(r,d,at);return r;}
void xx_step_part21_destroy(xx_step_part21 *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_step_part21_free(xx_step_part21 *r) {if(r){xx_step_part21_destroy(r);xx_mem_free(r);}}
bool xx_step_part21_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_step_part21_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
