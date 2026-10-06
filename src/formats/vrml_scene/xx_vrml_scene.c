/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.web3d.org/documents/specifications/14772/V2.0/part1/nodesRef.html
 * VRML97 bounded complete static node subset: Shape/Appearance/Material/IndexedFaceSet/Coordinate/Group/Transform and basic primitive/view/light metadata, typed fields, finite vectors, bounded indexes, legal node contexts, normalized rotation axes and balanced structure. Original encoded root nodes exported. DEF/USE, scripting, PROTO/ROUTE, textures and other nodes/fields declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/vrml_scene/xx_vrml_scene.h"
#include "../adobe_acb/xx_thirteenth_games.h"
static bool tg_quick(Abstractformat *f,uint64_t n) {uint8_t b[15];return n>=24&&pm_read(f,0,b,15)&&tg_tag(b,"#VRML V2.0 utf8",15);}
static bool vrml_vec(tg_lex *q,unsigned size,bool color,bool positive) {unsigned i;double v;for(i=0;i<size;++i)if(!tg_number(q,&v)||(color&&(v<0||v>1))||(positive&&v<=0))return false;return true;}
static bool vrml_bool(tg_lex *q) {return tg_kw(q,"TRUE")||tg_kw(q,"FALSE");}
static bool vrml_strings(tg_lex *q,bool navigation) {bool array=tg_char(q,'[');unsigned count=0;do {uint64_t p,z;if(array&&tg_char(q,']'))return true;if(++count>256||!tg_quoted(q,'"',&p,&z))return false;if(navigation&&!((z==7&&tg_tag(q->b+p,"EXAMINE",7))||(z==4&&(tg_tag(q->b+p,"WALK",4)||tg_tag(q->b+p,"NONE",4)))||(z==3&&(tg_tag(q->b+p,"FLY",3)||tg_tag(q->b+p,"ANY",3)))||(z==6&&tg_tag(q->b+p,"LOOKAT",6))))return false;if(!array)return true;}while(count<=256);return false;}
static bool vrml_child(unsigned kind) {return kind==1||kind==2||kind==3||(kind>=10&&kind<=13);}
static bool vrml_rotation(tg_lex *q) {double x,y,z,v,length;if(!tg_number(q,&x)||!tg_number(q,&y)||!tg_number(q,&z)||!tg_number(q,&v))return false;length=x*x+y*y+z*z;return length>0.999&&length<1.001;}
static bool vrml_node(tg_lex *q,unsigned depth,unsigned expected,uint32_t *points,unsigned *kindout) {
 unsigned kind,fields=0;uint32_t coords=0,maxindex=0,indices=0,faces=0,facepoints=0;bool haveindex=false;uint64_t id,z;
 if(depth>32)return false;
 if(tg_kw(q,"Group"))kind=1;else if(tg_kw(q,"Transform"))kind=2;else if(tg_kw(q,"Shape"))kind=3;else if(tg_kw(q,"Appearance"))kind=4;else if(tg_kw(q,"Material"))kind=5;else if(tg_kw(q,"Sphere"))kind=6;else if(tg_kw(q,"Box"))kind=7;else if(tg_kw(q,"Coordinate"))kind=8;else if(tg_kw(q,"IndexedFaceSet"))kind=9;else if(tg_kw(q,"WorldInfo"))kind=10;else if(tg_kw(q,"NavigationInfo"))kind=11;else if(tg_kw(q,"DirectionalLight"))kind=12;else if(tg_kw(q,"Viewpoint"))kind=13;else if(tg_kw(q,"Cone"))kind=14;else if(tg_kw(q,"Cylinder"))kind=15;else return false;
 if((expected&&kind!=expected)||!tg_char(q,'{'))return false;
 while(!tg_char(q,'}')){unsigned field=0,mode=0;double v;uint32_t unused=0;unsigned child=0;
  if(tg_stop(q->pd)||!tg_ident(q,&id,&z))return false;
#define VF(name,number,type) if(z==sizeof(name)-1&&tg_tag(q->b+id,name,sizeof(name)-1)){field=number;mode=type;}
  if(kind==1||kind==2){VF("children",1,1) else VF("bboxCenter",2,3) else VF("bboxSize",3,3)
   else if(kind==2){VF("translation",4,3) else VF("rotation",5,4) else VF("scale",6,13) else VF("center",7,3) else VF("scaleOrientation",8,4)}
  }else if(kind==3){VF("geometry",1,2) else VF("appearance",2,14)}
  else if(kind==4){VF("material",1,15)}
  else if(kind==5){VF("diffuseColor",1,5) else VF("emissiveColor",2,5) else VF("specularColor",3,5) else VF("ambientIntensity",4,6) else VF("shininess",5,6) else VF("transparency",6,6)}
  else if(kind==6){VF("radius",1,7)}
  else if(kind==7){VF("size",1,13)}
  else if(kind==8){VF("point",1,8)}
  else if(kind==9){VF("coord",1,16) else VF("coordIndex",2,9) else VF("ccw",3,10) else VF("solid",4,10) else VF("convex",5,10) else VF("creaseAngle",6,17)}
  else if(kind==10){VF("title",1,11) else VF("info",2,12)}
  else if(kind==11){VF("headlight",1,10) else VF("type",2,18) else VF("avatarSize",3,3) else VF("speed",4,7) else VF("visibilityLimit",5,19)}
  else if(kind==12){VF("direction",1,3) else VF("color",2,5) else VF("intensity",3,6) else VF("ambientIntensity",4,6) else VF("on",5,10)}
  else if(kind==13){VF("position",1,3) else VF("orientation",2,4) else VF("fieldOfView",3,20) else VF("description",4,11) else VF("jump",5,10)}
  else if(kind==14){VF("bottomRadius",1,7) else VF("height",2,7) else VF("bottom",3,10) else VF("side",4,10)}
  else if(kind==15){VF("radius",1,7) else VF("height",2,7) else VF("bottom",3,10) else VF("top",4,10) else VF("side",5,10)}
#undef VF
  if(!field||(fields&(1U<<field))) {return false; } fields|=1U<<field;
  if(mode==1){unsigned count=0;bool array=tg_char(q,'[');if(array&&tg_char(q,']'))continue;do {if(++count>4096||!vrml_node(q,depth+1,0,&unused,&child)||!vrml_child(child))return false;if(array&&tg_char(q,']'))break;if(!array)break;}while(count<=4096);}
  else if(mode==2){if(!vrml_node(q,depth+1,0,&unused,&child)||(child!=6&&child!=7&&child!=9&&child!=14&&child!=15))return false;}
  else if(mode==14||mode==15||mode==16){if(!vrml_node(q,depth+1,mode==14?4:mode==15?5:8,&unused,&child))return false;if(mode==16)coords=unused;}
  else if(mode==4){if(!vrml_rotation(q))return false;}
  else if(mode==3||mode==5||mode==13){if(!vrml_vec(q,3,mode==5,mode==13))return false;}
  else if(mode==6||mode==7||mode==17||mode==19||mode==20){if(!tg_number(q,&v)||(mode==6&&(v<0||v>1))||(mode==7&&v<=0)||(mode==17&&(v<0||v>3.141593))||(mode==19&&v<0)||(mode==20&&(v<=0||v>=3.141593)))return false;}
  else if(mode==10){if(!vrml_bool(q))return false;}
  else if(mode==11){if(!tg_quoted(q,'"',NULL,NULL))return false;}
  else if(mode==12||mode==18){if(!vrml_strings(q,mode==18))return false;}
  else if(mode==8){if(!tg_char(q,'['))return false;while(!tg_char(q,']')){if(++coords>100000||!vrml_vec(q,3,false,false))return false;}if(!coords)return false;}
  else if(mode==9){int32_t index;if(!tg_char(q,'['))return false;while(!tg_char(q,']')){if(++indices>300000||!tg_integer(q,&index)||index<-1)return false;if(index==-1){if(facepoints<3)return false;facepoints=0;if(++faces>100000)return false;}else {if((uint32_t)index>maxindex)maxindex=(uint32_t)index;haveindex=true;++facepoints;}}if(facepoints){if(facepoints<3)return false;++faces;}}
 }
 if(kind==8&&(!coords||(fields&(1<<1))==0)) {return false; } if(kind==9&&(!coords||!faces||!haveindex||maxindex>=coords))return false;
 if(kind==3&&!(fields&(1<<1))) {return false; } if(points)*points=coords;if(kindout)*kindout=kind;return true;
}
static bool tg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tg_text line={b,0,n,0,0,0};tg_lex q;unsigned roots=0;uint32_t points;unsigned kind;
 if(!tg_utf(b,n,false,pd)||!tg_line(&line)||line.stop<15||!tg_tag(b,"#VRML V2.0 utf8",15)||(line.stop>15&&b[15]!=32&&b[15]!=9)||!tg_emit(f,s,"descriptor.wrl",0,line.p,n))return false;
 q.b=b;q.p=line.p;q.n=n;q.pd=pd;q.work=0;q.hash=true;q.commas=true;q.comments=false;
 while(!tg_end(&q)){uint64_t start;if(!tg_skip(&q))return false;start=q.p;if(++roots>4093||!vrml_node(&q,0,0,&points,&kind)||!vrml_child(kind)||!tg_emit(f,s,"node.wrl",start,q.p-start,n))return false;}
 if(!roots||!tg_cover(f,s,"comments.wrl",n)) {return false; } s->size=(int64_t)n;return true;
}

void xx_vrml_scene_init(xx_vrml_scene *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_VRML_SCENE,"wrl");}}
xx_vrml_scene *xx_vrml_scene_create(xx_io_device *d,int64_t at) {xx_vrml_scene *r=(xx_vrml_scene *)xx_mem_alloc(sizeof(*r));if(r)xx_vrml_scene_init(r,d,at);return r;}
void xx_vrml_scene_destroy(xx_vrml_scene *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_vrml_scene_free(xx_vrml_scene *r) {if(r){xx_vrml_scene_destroy(r);xx_mem_free(r);}}
bool xx_vrml_scene_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_vrml_scene_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
