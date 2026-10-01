/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/KiCad/kicad-source-mirror/master/pcbnew/exporters/excellon_writer.cpp
 * Excellon drill subset: complete M48 header, INCH/METRIC zero-suppression and tool diameters, resolved tool selections and six-digit integer drill positions (INCH2:4/METRIC3:3, leading or trailing suppression) terminated by M30. Original encoded tools/positions exported; routing/repeat/machine-control extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/excellon_drill/xx_excellon_drill.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[3];return n>=16&&pm_read(f,0,b,3)&&tg_tag(b,"M48",3);}
static bool drill_coord(tg_lex *q,unsigned digits,bool trailing,int32_t *value) {uint64_t start,p;unsigned count=0;bool neg=false;int32_t v;if(q->p<q->n&&(q->b[q->p]=='-'||q->b[q->p]=='+'))neg=q->b[q->p++]=='-';start=p=q->p;while(p<q->n&&q->b[p]>='0'&&q->b[p]<='9'){++count;++p;}if(!count||count>digits)return false;q->p=start;if(!tg_integer(q,&v))return false;if(trailing)while(count++<digits)v*=10;*value=neg?-v:v;return true;}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tg_text line={b,0,n,0,0,0};uint8_t tools[1000];bool unit=false,trailing=false,body=false,ended=false,pos=false;unsigned digits=6,hits=0;int32_t selected=0,x=0,y=0;xx_mem_zero(tools,sizeof(tools));
 if(!tg_utf(b,n,true,pd)||!tg_line(&line)||!tg_word(&line,"M48")||!tg_done(&line)||!tg_emit(f,s,"header.drl",0,line.p,n))return false;
 while(line.p<n){tg_lex q;int32_t id;bool cx=false,cy=false;if(tg_stop(pd)||!tg_line(&line))return false;tg_space(&line);if(line.t==line.stop||b[line.t]==';')continue;if(ended)return false;q.b=b;q.p=line.t;q.n=line.stop;q.pd=pd;q.work=0;q.hash=false;q.commas=false;q.comments=false;
  if(!body){
   if(tg_kw(&q,"INCH")||tg_kw(&q,"METRIC")){if(unit||!tg_char(&q,','))return false;if(tg_kw(&q,"LZ"))trailing=false;else if(tg_kw(&q,"TZ"))trailing=true;else return false;if(!tg_end(&q))return false;unit=true;}
   else if(tg_char(&q,'T')){double diameter;if(!unit||!tg_integer(&q,&id)||id<1||id>999||tools[id]||!tg_char(&q,'C')||!tg_number(&q,&diameter)||diameter<=0||diameter>1000000||!tg_end(&q))return false;tools[id]=1;}
   else if(tg_char(&q,'%')||tg_kw(&q,"M95")){if(!unit||!tg_end(&q))return false;body=true;}
   else return false;
  }else {
   if(tg_char(&q,'T')){if(!tg_integer(&q,&id)||id<1||id>999||!tools[id]||!tg_end(&q))return false;selected=id;}
   else if(tg_kw(&q,"M30")){if(!hits||!tg_end(&q))return false;ended=true;}
   else {if(tg_char(&q,'X')){if(!drill_coord(&q,digits,trailing,&x))return false;cx=true;}if(tg_char(&q,'Y')){if(!drill_coord(&q,digits,trailing,&y))return false;cy=true;}if(!selected||(!cx&&!cy)||(!pos&&(!cx||!cy))||!tg_end(&q))return false;pos=true;++hits;}
  }
  if(!tg_emit(f,s,body?"drill-command.drl":"tool-descriptor.drl",line.start,line.p-line.start,n))return false;
 }
 if(!ended||!tg_cover(f,s,"comments.drl",n))return false;s->size=(int64_t)n;return true;
}

void xx_excellon_drill_init(xx_excellon_drill *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_EXCELLON_DRILL,"drl");}}
xx_excellon_drill *xx_excellon_drill_create(xx_io_device *d,int64_t at) {xx_excellon_drill *r=(xx_excellon_drill *)xx_mem_alloc(sizeof(*r));if(r)xx_excellon_drill_init(r,d,at);return r;}
void xx_excellon_drill_destroy(xx_excellon_drill *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_excellon_drill_free(xx_excellon_drill *r) {if(r){xx_excellon_drill_destroy(r);xx_mem_free(r);}}
bool xx_excellon_drill_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_excellon_drill_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
