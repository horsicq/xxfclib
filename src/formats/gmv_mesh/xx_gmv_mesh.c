/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/lanl/LaGriT/master/src/readgmv_binary.f
 * LANL GMV unstructured mesh subset: ASCII and little-endian IEEE32 binary nodes, fixed-arity cells, counted material/scalar variables and cycle/time records; binary additionally validates velocity, flags and polygon records with 2 to64 finite points, including LaGriT line exports. Complete local cardinalities and terminal endgmv are checked. Original typed mesh/data sections exported; structured/general-polyhedron/unknown extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/gmv_mesh/xx_gmv_mesh.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t b[8];return n>=24&&pm_read(f,0,b,8)&&fg_tag(b,"gmvinput",8);}
static bool fg_gmv_text(const uint8_t *p,const char *s) {size_t z=xx_rt_strlen(s),i;if(z>8||!fg_tag(p,s,z))return false;for(i=z;i<8;++i)if(p[i]!=32)return false;return true;}
static bool fg_gmv_name(const uint8_t *p) {unsigned i;bool ended=false,have=false;for(i=0;i<8;++i){if(p[i]==32){ended=true;continue;}if(ended||!fg_ident_char(p[i]))return false;have=true;}return have;}
static unsigned fg_gmv_arity(const uint8_t *p) {if(fg_gmv_text(p,"line"))return 2;if(fg_gmv_text(p,"tri"))return 3;if(fg_gmv_text(p,"quad")||fg_gmv_text(p,"tet"))return 4;if(fg_gmv_text(p,"pyramid"))return 5;if(fg_gmv_text(p,"prism"))return 6;if(fg_gmv_text(p,"hex"))return 8;return 0;}
static bool fg_gmv_binary(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_bin q={b,16,n,pd};uint32_t nodes=0,cells=0,seen=0,materials=0;const uint8_t *p;unsigned section=0;char label[48];bool ended=false;
 if(!fg_emit(f,s,"descriptor.gmv",0,16,n))return false;
 while(q.p<n){uint64_t start=q.p;uint32_t kind=0,count,i,location;unsigned arity;if(!fg_take(&q,8,&p))return false;
 if(fg_gmv_text(p,"nodes")){kind=1;if(nodes||!fg_count(&q,1000000,&nodes)||nodes<3||!fg_floats(&q,nodes*3))return false;}
 else if(fg_gmv_text(p,"cells")){kind=2;if(!nodes||cells||!fg_count(&q,1000000,&cells)||!cells)return false;for(i=0;i<cells;++i){uint32_t j,index,refs[8];if(!fg_take(&q,8,&p)||(arity=fg_gmv_arity(p))==0||!fg_count(&q,8,&count)||count!=arity)return false;for(j=0;j<count;++j){unsigned k;if(!fg_take(&q,4,&p)||(index=pm_le32(p))<1||index>nodes)return false;for(k=0;k<j;++k)if(refs[k]==index)return false;refs[j]=index;}}}
 else if(fg_gmv_text(p,"material")){kind=4;if(!cells||!fg_count(&q,4096,&materials)||!materials||!fg_count(&q,1,&location))return false;for(i=0;i<materials;++i)if(!fg_take(&q,8,&p)||!fg_gmv_name(p))return false;count=location?nodes:cells;for(i=0;i<count;++i)if(!fg_take(&q,4,&p)||pm_le32(p)>materials)return false;}
 else if(fg_gmv_text(p,"variable")){kind=8;if(!cells)return false;count=0;while(true){if(!fg_take(&q,8,&p))return false;if(fg_gmv_text(p,"endvars"))break;if(!fg_gmv_name(p)||++count>4096||!fg_count(&q,1,&location)||!fg_floats(&q,location?nodes:cells))return false;}if(!count)return false;}
 else if(fg_gmv_text(p,"velocity")){kind=16;if(!cells||!fg_count(&q,1,&location)||!fg_floats(&q,(location?nodes:cells)*3))return false;}
 else if(fg_gmv_text(p,"flags")){kind=32;if(!cells)return false;count=0;while(true){uint32_t types;if(!fg_take(&q,8,&p))return false;if(fg_gmv_text(p,"endflag"))break;if(!fg_gmv_name(p)||++count>4096||!fg_count(&q,4096,&types)||!types||!fg_count(&q,1,&location))return false;for(i=0;i<types;++i)if(!fg_take(&q,8,&p)||!fg_gmv_name(p))return false;for(i=0;i<(location?nodes:cells);++i)if(!fg_take(&q,4,&p)||pm_le32(p)>types)return false;}if(!count)return false;}
 else if(fg_gmv_text(p,"polygons")){kind=64;count=0;while(true){uint32_t color,points;if(!fg_span(q.p,8,n))return false;if(fg_gmv_text(b+q.p,"endpoly")){q.p+=8;break;}if(++count>1000000||!fg_count(&q,4096,&color)||!color||(materials&&color>materials)||!fg_count(&q,64,&points)||points<2||!fg_floats(&q,points*3))return false;}if(!count)return false;}
 else if(fg_gmv_text(p,"cycleno")){kind=128;if(!fg_count(&q,2147483647,&count))return false;}
 else if(fg_gmv_text(p,"probtime")){kind=256;double v;if(!fg_float(&q,&v)||v<0)return false;}
 else if(fg_gmv_text(p,"endgmv")){ended=true;if(!nodes||!cells||q.p!=n)return false;}
 else { return false; } if(kind&&(seen&kind))return false;seen|=kind;xx_rt_snprintf(label,sizeof(label),"section-%u.gmv",section++);if(!fg_emit(f,s,label,start,q.p-start,n))return false;if(ended)break;
 }
 return ended&&q.p==n;
}
static bool fg_gmv_ascii(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_lex q={b,0,n,pd,0,false,false,false};int32_t nodes=0,cells=0,count,id;unsigned section=0;uint32_t seen=0;double v;bool ended=false;char label[48];
 if(!fg_utf(b,n,true,pd)||!fg_kw(&q,"gmvinput")||!fg_kw(&q,"ascii")||!fg_emit(f,s,"descriptor.gmv",0,q.p,n))return false;
 while(fg_skip(&q)&&q.p<n){uint64_t start=q.p;unsigned kind=0;int32_t i,j;
 if(fg_kw(&q,"nodes")){kind=1;if(nodes||!fg_integer(&q,&nodes)||nodes<3||nodes>1000000)return false;for(i=0;i<nodes*3;++i)if(!fg_number(&q,&v))return false;}
 else if(fg_kw(&q,"cells")){kind=2;if(!nodes||cells||!fg_integer(&q,&cells)||cells<1||cells>1000000)return false;for(i=0;i<cells;++i){unsigned arity=0;if(fg_kw(&q,"line"))arity=2;else if(fg_kw(&q,"tri"))arity=3;else if(fg_kw(&q,"quad")||fg_kw(&q,"tet"))arity=4;else if(fg_kw(&q,"pyramid"))arity=5;else if(fg_kw(&q,"prism"))arity=6;else if(fg_kw(&q,"hex"))arity=8;else return false;if(!fg_integer(&q,&count)||(unsigned)count!=arity)return false;for(j=0;j<count;++j)if(!fg_integer(&q,&id)||id<1||id>nodes)return false;}}
 else if(fg_kw(&q,"material")){int32_t location;uint64_t at,z;kind=4;if(!cells||!fg_integer(&q,&count)||count<1||count>4096||!fg_integer(&q,&location)||location<0||location>1)return false;for(i=0;i<count;++i)if(!fg_ident(&q,&at,&z)||z>8)return false;for(i=0;i<(location?nodes:cells);++i)if(!fg_integer(&q,&id)||id<0||id>count)return false;}
 else if(fg_kw(&q,"variable")){uint64_t at,z;int32_t location;unsigned vars=0;kind=8;if(!cells)return false;while(!fg_kw(&q,"endvars")){if(++vars>4096||!fg_ident(&q,&at,&z)||z>8||!fg_integer(&q,&location)||location<0||location>1)return false;for(i=0;i<(location?nodes:cells);++i)if(!fg_number(&q,&v))return false;}if(!vars)return false;}
 else if(fg_kw(&q,"cycleno")){kind=128;if(!fg_integer(&q,&id)||id<0)return false;}
 else if(fg_kw(&q,"probtime")){kind=256;if(!fg_number(&q,&v)||v<0)return false;}
 else if(fg_kw(&q,"endgmv")){if(!nodes||!cells||!fg_end(&q))return false;ended=true;}
 else { return false; } if(kind&&(seen&kind))return false;seen|=kind;xx_rt_snprintf(label,sizeof(label),"section-%u.gmv",section++);if(!fg_emit(f,s,label,start,q.p-start,n))return false;if(ended)break;
 }
 return ended&&fg_end(&q)&&fg_cover(f,s,"framing.gmv",n);
}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {if(n<16||!fg_tag(b,"gmvinput",8))return false;if(fg_tag(b+8,"ieee    ",8))return fg_gmv_binary(f,s,b,n,pd);return fg_gmv_ascii(f,s,b,n,pd);}

void xx_gmv_mesh_init(xx_gmv_mesh *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_GMV_MESH,"gmv");}}
xx_gmv_mesh *xx_gmv_mesh_create(xx_io_device *d,int64_t at) {xx_gmv_mesh *r=(xx_gmv_mesh *)xx_mem_alloc(sizeof(*r));if(r)xx_gmv_mesh_init(r,d,at);return r;}
void xx_gmv_mesh_destroy(xx_gmv_mesh *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gmv_mesh_free(xx_gmv_mesh *r) {if(r){xx_gmv_mesh_destroy(r);xx_mem_free(r);}}
bool xx_gmv_mesh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_gmv_mesh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
