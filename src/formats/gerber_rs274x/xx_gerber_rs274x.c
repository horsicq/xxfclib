/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.ucamco.com/en/sales/downloads
 * Gerber RS274X linear subset: complete FS/MO and basic C/R/O/P aperture declarations, modal coordinate/tool state, G01 draw/flash and bounded closed G36/G37 regions, polarity plus typed X2 attributes and terminal M02. Original encoded commands exported. Aperture macros, arcs, transformations, blocks/repeats and rendering declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/gerber_rs274x/xx_gerber_rs274x.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b;return n>=24&&pm_read(f,0,&b,1)&&(b=='G'||b=='%'||b==32||b==10||b==13);}
static bool gerber_coordinate(tg_lex *q,unsigned digits,bool trailing,int32_t *value) {uint64_t start,p;unsigned count=0;int32_t v;bool neg=false;if(q->p<q->n&&(q->b[q->p]=='-'||q->b[q->p]=='+'))neg=q->b[q->p++]=='-';start=p=q->p;while(p<q->n&&q->b[p]>='0'&&q->b[p]<='9'){++count;++p;}if(!count||count>digits)return false;q->p=start;if(!tg_integer(q,&v))return false;if(trailing)while(count++<digits)v*=10;*value=neg?-v:v;return true;}
static bool gerber_aperture(tg_lex *q,uint8_t *tools) {int32_t id,vertices;uint8_t shape;double a,b,hole=0,rotation=0;if(!tg_integer(q,&id)||id<10||id>999||tools[id]||q->p==q->n)return false;shape=q->b[q->p++];if(!tg_char(q,',')||!tg_number(q,&a)||a<=0||a>1000000)return false;
 if(shape=='R'||shape=='O'){if(!tg_char(q,'X')||!tg_number(q,&b)||b<=0||b>1000000)return false;if(tg_char(q,'X')&&(!tg_number(q,&hole)||hole<0||hole>=a||hole>=b))return false;}
 else if(shape=='P'){if(!tg_char(q,'X')||!tg_integer(q,&vertices)||vertices<3||vertices>12)return false;if(tg_char(q,'X')){if(!tg_number(q,&rotation)||rotation<-360000||rotation>360000)return false;if(tg_char(q,'X')&&(!tg_number(q,&hole)||hole<0||hole>=a))return false;}}
 else if(shape=='C'){if(tg_char(q,'X')&&(!tg_number(q,&hole)||hole<0||hole>=a))return false;}else return false;
 if(!tg_end(q))return false;tools[id]=1;return true;
}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=0;uint8_t tools[1000];unsigned digits=0,ops=0,regionpoints=0;int32_t tool=0,x=0,y=0,firstx=0,firsty=0,operation=2;double area=0;bool format=false,units=false,trailing=false,pos=false,region=false,ended=false;xx_mem_zero(tools,sizeof(tools));
 if(!tg_utf(b,n,true,pd))return false;
 while(p<n){uint64_t start=p,at,end;bool extended;tg_lex q;int32_t code=0,nx=x,ny=y;bool cx=false,cy=false;
  if(tg_stop(pd))return false;while(p<n&&(b[p]==32||b[p]==9||b[p]==10||b[p]==13))++p;if(p==n)break;if(ended)return false;
  extended=b[p]=='%';if(extended)++p;at=p;while(p<n&&b[p]!='*'){if(p-at>8192)return false;++p;}if(p==n)return false;end=p++;if(extended&&(p==n||b[p++]!='%'))return false;
  q.b=b;q.p=at;q.n=end;q.pd=pd;q.work=0;q.hash=false;q.commas=false;q.comments=false;
  if(extended){
   if(tg_kw(&q,"FSLAX24Y24")){if(format||!tg_end(&q))return false;digits=6;format=true;}
   else if(end-at==10&&b[at]=='F'&&b[at+1]=='S'&&(b[at+2]=='L'||b[at+2]=='T')&&b[at+3]=='A'&&b[at+4]=='X'&&b[at+7]=='Y'&&b[at+5]>='1'&&b[at+5]<='6'&&b[at+6]>='0'&&b[at+6]<='6'&&b[at+8]==b[at+5]&&b[at+9]==b[at+6]){if(format)return false;digits=b[at+5]-'0'+b[at+6]-'0';if(digits>9)return false;trailing=b[at+2]=='T';format=true;}
   else if(tg_kw(&q,"MOMM")||tg_kw(&q,"MOIN")){if(units||!tg_end(&q))return false;units=true;}
   else if(tg_char(&q,'A')&&tg_char(&q,'D')&&tg_char(&q,'D')){if(!format||!units||region||!gerber_aperture(&q,tools))return false;}
   else if(tg_kw(&q,"LPD")||tg_kw(&q,"LPC")){if(region||!tg_end(&q))return false;}
   else if(tg_char(&q,'T')){uint8_t a;if(q.p==q.n)return false;a=b[q.p++];if(a!='F'&&a!='A'&&a!='D')return false;if(q.p==q.n){if(a!='D')return false;}else {uint64_t id,z;if(!tg_ident(&q,&id,&z)){if(!tg_char(&q,'.')||!tg_ident(&q,&id,&z))return false;}while(q.p<q.n){uint8_t c=b[q.p++];if(c<32||c>126||c=='%'||c=='*')return false;}}}
   else return false;
  }else {
   if(end-at>=3&&tg_tag(b+at,"G04",3)){q.p=q.n;}
   else if(tg_kw(&q,"M02")){if(region||!ops||!tg_end(&q))return false;ended=true;}
   else if(tg_kw(&q,"G36")){if(region||!format||!units||!tg_end(&q))return false;region=true;regionpoints=0;area=0;}
   else if(tg_kw(&q,"G37")){if(!region||regionpoints<4||x!=firstx||y!=firsty||area==0||!tg_end(&q))return false;region=false;++ops;}
   else {
    if(tg_span(q.p,3,q.n)&&tg_tag(b+q.p,"G01",3))q.p+=3;
    if(tg_char(&q,'X')){if(!format||!gerber_coordinate(&q,digits,trailing,&nx))return false;cx=true;}
    if(tg_char(&q,'Y')){if(!format||!gerber_coordinate(&q,digits,trailing,&ny))return false;cy=true;}
    if(tg_char(&q,'D')){if(!tg_integer(&q,&code)||code<1||code>999)return false;}
    if(!tg_end(&q))return false;
    if((cx||cy)&&!code)code=operation;
    if(code>=10){if(cx||cy||!tools[code]||region)return false;tool=code;}
    else if(cx||cy||code){if(!format||!units||!tool||(!pos&&(!cx||!cy))||code<1||code>3)return false;
     if(region){if(code==3)return false;if(code==2){if(regionpoints&&(regionpoints<4||x!=firstx||y!=firsty||area==0))return false;firstx=nx;firsty=ny;regionpoints=1;area=0;}else {if(!regionpoints||++regionpoints>100000)return false;area+=(double)x*ny-(double)nx*y;}}
     else if(code==1||code==3)++ops;
     x=nx;y=ny;pos=true;operation=code;
    }else if(end-at!=3||!tg_tag(b+at,"G01",3))return false;
   }
  }
  if(!tg_emit(f,s,"command.gbr",start,p-start,n))return false;
 }
 if(!ended||!format||!units||!ops||!tg_cover(f,s,"whitespace.gbr",n))return false;s->size=(int64_t)n;return true;
}

void xx_gerber_rs274x_init(xx_gerber_rs274x *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_GERBER_RS274X,"gbr");}}
xx_gerber_rs274x *xx_gerber_rs274x_create(xx_io_device *d,int64_t at) {xx_gerber_rs274x *r=(xx_gerber_rs274x *)xx_mem_alloc(sizeof(*r));if(r)xx_gerber_rs274x_init(r,d,at);return r;}
void xx_gerber_rs274x_destroy(xx_gerber_rs274x *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gerber_rs274x_free(xx_gerber_rs274x *r) {if(r){xx_gerber_rs274x_destroy(r);xx_mem_free(r);}}
bool xx_gerber_rs274x_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_gerber_rs274x_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
