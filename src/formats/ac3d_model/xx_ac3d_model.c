/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://www.inivis.com/ac3d/man/ac3dfileformat.html
 * AC3Db ASCII material palette and complete bounded world/group/poly hierarchy, finite transforms/vertices, surface flags/material references and complete indexed UV records. Unknown fields, lights, subdivision and external resources unsupported. Original typed sections remain encoded.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/ac3d_model/xx_ac3d_model.h"
#include "../wavefront_obj/xx_eleventh_media.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[5];return n>=45&&pm_read(f,0,b,5)&&eg_tag(b,"AC3Db",5);}
static bool ac_next(eg_text *q) {while(q->p<q->end){if(!eg_line(q))return false;if(!eg_done(q))return true;}return false;}
typedef struct ac_state {Abstractformat *f;pm_stream *s;eg_text q;xx_pd_struct *pd;uint32_t materials,objects,polygons;} ac_state;
static bool ac_material(ac_state *a) {
 eg_text *q=&a->q;unsigned i,j;double v;int32_t u;const char *keys[]={"rgb","amb","emis","spec"};
 if(!eg_string(q))return false;
 for(i=0;i<4;++i){if(!eg_word(q,keys[i]))return false;for(j=0;j<3;++j)if(!eg_num(q,&v)||v<0||v>1)return false;}
 if(!eg_word(q,"shi")||!eg_i(q,&u)||u<0||u>128||!eg_word(q,"trans")||!eg_num(q,&v)||v<0||v>1||!eg_done(q)||++a->materials>1024)return false;
 return eg_emit(a->f,a->s,"material.ac",q->start,q->p-q->start,q->end);
}
static bool ac_hex(eg_text *q,uint32_t *v) {
 uint64_t p;uint32_t u=0;unsigned digits=0;eg_space(q);p=q->t;
 if(!eg_span(p,2,q->stop)||q->b[p]!='0'||q->b[p+1]!='x')return false;p+=2;
 while(p<q->stop){uint8_t c=q->b[p];unsigned d;if(c>='0'&&c<='9')d=c-'0';else if(c>='a'&&c<='f')d=c-'a'+10;else if(c>='A'&&c<='F')d=c-'A'+10;else break;if(++digits>8)return false;u=(u<<4)|d;++p;}
 if(!digits)return false;q->t=p;*v=u;return true;
}
static bool ac_object(ac_state *a,unsigned depth) {
 eg_text *q=&a->q;uint64_t start=q->start;uint32_t seen=0;int32_t vertices=0,surfaces=0,kids=0;bool poly=false;char label[64];uint32_t id=a->objects++;
 if(depth>32||a->objects>1024||!eg_word(q,"OBJECT"))return false;
 if(eg_word(q,"poly"))poly=true;else if(!eg_word(q,"world")&&!eg_word(q,"group"))return false;if(!eg_done(q))return false;
 for(;;){uint32_t bit=0;if(eg_stop(a->pd)||!ac_next(q))return false;
  if(eg_word(q,"kids")){if(!eg_i(q,&kids)||kids<0||kids>1024||!eg_done(q))return false;break;}
  if(eg_word(q,"name")){bit=1;if(!eg_string(q)||!eg_done(q))return false;}
  else if(eg_word(q,"texture")){bit=2;if(!eg_string(q)||!eg_done(q))return false;}
  else if(eg_word(q,"loc")){bit=4;if(!eg_nums(q,3))return false;}
  else if(eg_word(q,"rot")){bit=8;if(!eg_nums(q,9))return false;}
  else if(eg_word(q,"texrep")||eg_word(q,"texoff")){if(!eg_nums(q,2))return false;}
  else if(eg_word(q,"url")){if(!eg_string(q)||!eg_done(q))return false;}
  else if(eg_word(q,"numvert")){int32_t i;bit=16;if(!eg_i(q,&vertices)||vertices<0||vertices>1000000||!eg_done(q))return false;
   for(i=0;i<vertices;++i)if(eg_stop(a->pd)||!ac_next(q)||!eg_nums(q,3))return false;}
  else if(eg_word(q,"numsurf")){int32_t i;bit=32;if(!(seen&16)||!eg_i(q,&surfaces)||surfaces<0||surfaces>1000000||!eg_done(q))return false;
   for(i=0;i<surfaces;++i){uint32_t flags;int32_t material,count,j,used[256];double uv;
    if(eg_stop(a->pd)||!ac_next(q)||!eg_word(q,"SURF")||!ac_hex(q,&flags)||(flags&~0x30U)||!eg_done(q)||!ac_next(q)||!eg_word(q,"mat")||!eg_i(q,&material)||material<0||(uint32_t)material>=a->materials||!eg_done(q)||!ac_next(q)||!eg_word(q,"refs")||!eg_i(q,&count)||count<3||count>256||!eg_done(q))return false;
    for(j=0;j<count;++j){int32_t k;if(!ac_next(q)||!eg_i(q,&used[j])||used[j]<0||used[j]>=vertices||!eg_num(q,&uv)||!eg_num(q,&uv)||!eg_done(q))return false;for(k=0;k<j;++k)if(used[k]==used[j])return false;}
   }}
  else return false;
  if(bit&&(seen&bit))return false;seen|=bit;
 }
 if(poly&&(vertices<3||surfaces<1||!(seen&32)))return false;if(!poly&&(vertices||surfaces))return false;a->polygons+=(uint32_t)surfaces;
 xx_rt_snprintf(label,sizeof(label),"object-%u.ac",id);if(!eg_emit(a->f,a->s,label,start,q->p-start,q->end))return false;
 while(kids--){if(!ac_next(q)||!ac_object(a,depth+1))return false;}return true;
}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 ac_state a;bool object=false;xx_mem_zero(&a,sizeof(a));a.f=f;a.s=s;a.pd=pd;a.q.b=b;a.q.end=n;
 if(!eg_utf(b,n,false,pd)||!ac_next(&a.q)||!eg_word(&a.q,"AC3Db")||!eg_done(&a.q)||!eg_emit(f,s,"descriptor.ac",0,a.q.p,n))return false;
 while(a.q.p<n){if(!ac_next(&a.q)){if(a.q.p<n)return false;break;}if(!object&&eg_word(&a.q,"MATERIAL")){if(!ac_material(&a))return false;}else{if(object||!ac_object(&a,0))return false;object=true;}}
 if(!object||!a.polygons)return false;
 {size_t i,count=s->count;uint64_t covered=0;for(i=0;i<count;++i){uint64_t at=(uint64_t)(s->items[i].offset-f->base_address),z=(uint64_t)s->items[i].size;if(at<covered)return false;if(at>covered&&!eg_emit(f,s,"comments.ac",covered,at-covered,n))return false;covered=at+z;}if(covered<n&&!eg_emit(f,s,"comments.ac",covered,n-covered,n))return false;}
 s->size=(int64_t)n;return true;
}

void xx_ac3d_model_init(xx_ac3d_model *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_AC3D_MODEL,"ac");}}
xx_ac3d_model *xx_ac3d_model_create(xx_io_device *d,int64_t at) {xx_ac3d_model *r=(xx_ac3d_model *)xx_mem_alloc(sizeof(*r));if(r)xx_ac3d_model_init(r,d,at);return r;}
void xx_ac3d_model_destroy(xx_ac3d_model *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ac3d_model_free(xx_ac3d_model *r) {if(r){xx_ac3d_model_destroy(r);xx_mem_free(r);}}
bool xx_ac3d_model_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_ac3d_model_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
