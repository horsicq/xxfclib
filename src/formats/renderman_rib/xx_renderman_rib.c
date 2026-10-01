/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/aqsis/aqsis/master/libs/riutil/ribparser.cpp
 * RenderMan ASCII static RIB subset: complete typed camera/display/transform/world/frame/attribute commands and Polygon/PointsPolygons/Sphere geometry, finite arrays, local indexes and balanced scopes; geometry P precedes optional N/Cs/Os/st arrays. Original encoded commands exported; shaders retained only as typed metadata, never loaded. Binary RIB, motion, archives/procedurals, custom parameters and rendering declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/renderman_rib/xx_renderman_rib.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b;return n>=24&&pm_read(f,0,&b,1)&&b>=9&&b<=126;}
static bool rib_numbers(tg_lex *q,unsigned count,bool positive,bool color) {unsigned i;double v;for(i=0;i<count;++i)if(!tg_number(q,&v)||(positive&&v<=0)||(color&&(v<0||v>1)))return false;return true;}
static bool rib_scalar(tg_lex *q,double *v) {bool array=tg_char(q,'[');return tg_number(q,v)&&(!array||tg_char(q,']'));}
static bool rib_array(tg_lex *q,uint32_t *count,bool integer,uint32_t *maximum,uint64_t *sum,bool color) {uint32_t used=0;double v;int32_t i;if(!tg_char(q,'['))return false;while(!tg_char(q,']')){if(++used>300000||tg_stop(q->pd))return false;if(integer){if(!tg_integer(q,&i)||i<0)return false;if(maximum&&(uint32_t)i>*maximum)*maximum=(uint32_t)i;if(sum)*sum+=(uint32_t)i;}else if(!tg_number(q,&v)||(color&&(v<0||v>1)))return false;}*count=used;return used>0;}
static bool rib_parameters(tg_lex *q,bool geometry,uint32_t *vertices) {
 unsigned fields=0;while(tg_skip(q)&&q->p<q->n&&q->b[q->p]=='"'){uint64_t p,z;unsigned kind;uint32_t count=0;double value;
  if(!tg_quoted(q,'"',&p,&z))return false;
  if(geometry){if(z==1&&q->b[p]=='P')kind=0;else if(z==1&&q->b[p]=='N')kind=1;else if(z==2&&tg_tag(q->b+p,"Cs",2))kind=2;else if(z==2&&tg_tag(q->b+p,"Os",2))kind=3;else if(z==2&&tg_tag(q->b+p,"st",2))kind=4;else return false;
   if(fields&(1U<<kind)||!rib_array(q,&count,false,NULL,NULL,kind==2||kind==3))return false;fields|=1U<<kind;
   if(!kind){if(count%3||count<9)return false;*vertices=count/3;}else if(!*vertices||count!=*vertices*(kind==4?2:3))return false;
  }else {if((z==2&&(tg_tag(q->b+p,"Ka",2)||tg_tag(q->b+p,"Kd",2)||tg_tag(q->b+p,"Ks",2)))||(z==9&&tg_tag(q->b+p,"roughness",9))||(z==6&&tg_tag(q->b+p,"sphere",6))){if(!rib_scalar(q,&value)||value<0||value>1000000)return false;}
   else if(z==11&&tg_tag(q->b+p,"compression",11)){if(!tg_quoted(q,'"',NULL,NULL))return false;}else return false;
  }
 }return !geometry||((fields&1)!=0);
}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tg_lex q={b,0,n,pd,0,true,false,false};uint8_t stack[64];unsigned depth=0;bool world=false,frame=false;unsigned worlds=0,geometry=0;
 if(!tg_utf(b,n,false,pd))return false;
 while(!tg_end(&q)){uint64_t start,p,z;double v;uint32_t vertices=0;int32_t integer;if(!tg_skip(&q))return false;start=q.p;
  if(tg_kw(&q,"WorldBegin")){if(world||depth>=64)return false;stack[depth++]=3;world=true;}
  else if(tg_kw(&q,"WorldEnd")){if(!world||!depth||stack[--depth]!=3)return false;world=false;++worlds;}
  else if(tg_kw(&q,"FrameBegin")){if(frame||world||depth||!tg_integer(&q,&integer)||integer<0)return false;stack[depth++]=4;frame=true;}
  else if(tg_kw(&q,"FrameEnd")){if(world||!frame||!depth||stack[--depth]!=4)return false;frame=false;}
  else if(tg_kw(&q,"AttributeBegin")){if(!world||depth>=64)return false;stack[depth++]=1;}
  else if(tg_kw(&q,"AttributeEnd")){if(!depth||stack[--depth]!=1)return false;}
  else if(tg_kw(&q,"TransformBegin")){if(depth>=64)return false;stack[depth++]=2;}
  else if(tg_kw(&q,"TransformEnd")){if(!depth||stack[--depth]!=2)return false;}
  else if(tg_kw(&q,"Format")){if(world||!tg_integer(&q,&integer)||integer<1||integer>65536||!tg_integer(&q,&integer)||integer<1||integer>65536||!rib_numbers(&q,1,true,false))return false;}
  else if(tg_kw(&q,"Display")){if(world||!tg_quoted(&q,'"',NULL,NULL)||!tg_quoted(&q,'"',NULL,NULL)||!tg_quoted(&q,'"',NULL,NULL)||!rib_parameters(&q,false,&vertices))return false;}
  else if(tg_kw(&q,"Projection")){if(world||!tg_quoted(&q,'"',&p,&z)||!((z==11&&tg_tag(b+p,"perspective",11))||(z==12&&tg_tag(b+p,"orthographic",12))))return false;if(tg_skip(&q)&&q.p<n&&b[q.p]=='"'){if(!tg_quoted(&q,'"',&p,&z)||z!=3||!tg_tag(b+p,"fov",3)||!rib_scalar(&q,&v)||v<=0||v>=180)return false;}}
  else if(tg_kw(&q,"PixelSamples")){if(world||!rib_numbers(&q,2,true,false))return false;}
  else if(tg_kw(&q,"PixelFilter")){if(world||!tg_quoted(&q,'"',NULL,NULL)||!rib_numbers(&q,2,true,false))return false;}
  else if(tg_kw(&q,"ShadingRate")){if(!rib_numbers(&q,1,true,false))return false;}
  else if(tg_kw(&q,"Translate")){if(!rib_numbers(&q,3,false,false))return false;}
  else if(tg_kw(&q,"Scale")){unsigned i;for(i=0;i<3;++i)if(!tg_number(&q,&v)||v==0)return false;}
  else if(tg_kw(&q,"Rotate")){double x,y,zv;if(!tg_number(&q,&v)||!tg_number(&q,&x)||!tg_number(&q,&y)||!tg_number(&q,&zv)||(x==0&&y==0&&zv==0))return false;}
  else if(tg_kw(&q,"Identity")){}
  else if(tg_kw(&q,"Transform")||tg_kw(&q,"ConcatTransform")){uint32_t count;if(!rib_array(&q,&count,false,NULL,NULL,false)||count!=16)return false;}
  else if(tg_kw(&q,"Color")||tg_kw(&q,"Opacity")){if(!world||!rib_numbers(&q,3,false,true))return false;}
  else if(tg_kw(&q,"Sides")){if(!tg_integer(&q,&integer)||(integer!=1&&integer!=2))return false;}
  else if(tg_kw(&q,"Orientation")){if(!tg_quoted(&q,'"',&p,&z)||!((z==2&&(tg_tag(b+p,"lh",2)||tg_tag(b+p,"rh",2)))||(z==6&&tg_tag(b+p,"inside",6))||(z==7&&tg_tag(b+p,"outside",7))))return false;}
  else if(tg_kw(&q,"Surface")||tg_kw(&q,"Displacement")){if(!world||!tg_quoted(&q,'"',NULL,NULL)||!rib_parameters(&q,false,&vertices))return false;}
  else if(tg_kw(&q,"Attribute")){if(!world||!tg_quoted(&q,'"',&p,&z)||z!=17||!tg_tag(b+p,"displacementbound",17)||!rib_parameters(&q,false,&vertices))return false;}
  else if(tg_kw(&q,"Polygon")){if(!world||!rib_parameters(&q,true,&vertices))return false;++geometry;}
  else if(tg_kw(&q,"PointsPolygons")){uint32_t count,maxindex=0,indexcount;uint64_t sum=0;uint64_t begin;
   if(!world)return false;begin=q.p;if(!rib_array(&q,&count,true,NULL,&sum,false)||count>100000||sum>300000)return false;
   {tg_lex check=q;check.p=begin;if(!tg_char(&check,'['))return false;while(!tg_char(&check,']'))if(!tg_integer(&check,&integer)||integer<3||integer>100000)return false;}
   if(!rib_array(&q,&indexcount,true,&maxindex,NULL,false)||indexcount!=sum||!rib_parameters(&q,true,&vertices)||maxindex>=vertices)return false;++geometry;
  }
  else if(tg_kw(&q,"Sphere")){double radius,zmin,zmax,theta;if(!world||!tg_number(&q,&radius)||radius<=0||!tg_number(&q,&zmin)||!tg_number(&q,&zmax)||zmin<-radius||zmax>radius||zmin>=zmax||!tg_number(&q,&theta)||theta<=0||theta>360)return false;++geometry;}
  else return false;
  if(!tg_emit(f,s,"command.rib",start,q.p-start,n))return false;
 }
 if(depth||world||frame||!worlds||!geometry||!tg_cover(f,s,"comments.rib",n))return false;s->size=(int64_t)n;return true;
}

void xx_renderman_rib_init(xx_renderman_rib *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_RENDERMAN_RIB,"rib");}}
xx_renderman_rib *xx_renderman_rib_create(xx_io_device *d,int64_t at) {xx_renderman_rib *r=(xx_renderman_rib *)xx_mem_alloc(sizeof(*r));if(r)xx_renderman_rib_init(r,d,at);return r;}
void xx_renderman_rib_destroy(xx_renderman_rib *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_renderman_rib_free(xx_renderman_rib *r) {if(r){xx_renderman_rib_destroy(r);xx_mem_free(r);}}
bool xx_renderman_rib_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_renderman_rib_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
