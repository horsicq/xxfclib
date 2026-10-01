/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/LoicMarechal/libMeshb/master/README.md
 * INRIA MEDIT ASCII mesh v1/v2: complete dimension, finite vertex table, typed edge/triangle/quad/tetra/hex connectivity and reference labels, counted Corners and RequiredVertices records. Original typed sections exported; binary/solution/high-order and other auxiliary extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/medit_mesh/xx_medit_mesh.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t b[20];return n>=24&&pm_read(f,0,b,20)&&fg_tag(b,"MeshVersionFormatted",20);}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_lex q={b,0,n,pd,0,true,false,false};int32_t version,dim,nv=0,count,index,label;uint32_t seen=0;unsigned section=0;uint64_t start;double v;bool ended=false,have_cells=false;
 if(!fg_utf(b,n,true,pd)||!fg_kw(&q,"MeshVersionFormatted")||!fg_integer(&q,&version)||(version!=1&&version!=2)||!fg_kw(&q,"Dimension")||!fg_integer(&q,&dim)||(dim!=2&&dim!=3)||!fg_emit(f,s,"descriptor.mesh",0,q.p,n))return false;
 while(fg_skip(&q)&&q.p<n){unsigned kind=0,arity=0;uint32_t bit;int32_t i;char name[48];start=q.p;if(fg_kw(&q,"End")){ended=true;if(!fg_emit(f,s,"terminator.mesh",start,q.p-start,n))return false;break;}
 if(fg_kw(&q,"Vertices")){kind=1;arity=(unsigned)dim;}
 else if(fg_kw(&q,"Edges")){kind=2;arity=2;}else if(fg_kw(&q,"Triangles")){kind=3;arity=3;}else if(fg_kw(&q,"Quadrilaterals")){kind=4;arity=4;}else if(fg_kw(&q,"Tetrahedra")){kind=5;arity=4;}else if(fg_kw(&q,"Hexahedra")){kind=6;arity=8;}
 else if(fg_kw(&q,"Corners")){kind=7;arity=1;}else if(fg_kw(&q,"RequiredVertices")){kind=8;arity=1;}else return false;
 bit=1U<<kind;if(seen&bit)return false;seen|=bit;
 if(!fg_integer(&q,&count)||count<1||count>1000000||(kind!=1&&!nv)||(dim==2&&(kind==5||kind==6)))return false;
 for(i=0;i<count;++i){unsigned k;if(fg_stop(pd))return false;for(k=0;k<arity;++k){if(kind==1){if(!fg_number(&q,&v))return false;}else if(!fg_integer(&q,&index)||index<1||index>nv)return false;}if(kind<=6&&(!fg_integer(&q,&label)||label<0))return false;}
 if(kind==1)nv=count;else if(kind<=6)have_cells=true;xx_rt_snprintf(name,sizeof(name),"section-%u.mesh",section++);if(!fg_emit(f,s,name,start,q.p-start,n))return false;
 }
 return nv&&have_cells&&(!ended||fg_end(&q))&&fg_end(&q)&&fg_cover(f,s,"comments.mesh",n);
}

void xx_medit_mesh_init(xx_medit_mesh *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_MEDIT_MESH,"mesh");}}
xx_medit_mesh *xx_medit_mesh_create(xx_io_device *d,int64_t at) {xx_medit_mesh *r=(xx_medit_mesh *)xx_mem_alloc(sizeof(*r));if(r)xx_medit_mesh_init(r,d,at);return r;}
void xx_medit_mesh_destroy(xx_medit_mesh *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_medit_mesh_free(xx_medit_mesh *r) {if(r){xx_medit_mesh_destroy(r);xx_mem_free(r);}}
bool xx_medit_mesh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_medit_mesh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
