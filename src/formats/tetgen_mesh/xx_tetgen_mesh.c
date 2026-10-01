/* SPDX-License-Identifier: MIT
 * Primary reference: https://wias-berlin.de/software/tetgen/1.5/doc/manual/manual006.html
 * TetGen standalone node/element sections: exact counted dimensions/attribute/marker columns, unique nonnegative IDs, finite coordinates/attributes and distinct bounded connectivity. Original descriptor and row records exported; node sidecars are not loaded and element references are retained, not resolved externally.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/tetgen_mesh/xx_tetgen_mesh.h"
#include "../wbmp_image/xx_fifteenth_games.h"
static bool vg_quick(Abstractformat *f,uint64_t n) {uint8_t c;return n>=4&&pm_read(f,0,&c,1)&&(c=='#'||c==' '||c=='\t'||c=='\n'||c=='\r'||(c>='0'&&c<='9'));}
static bool vg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {vg_text q={b,0,n,0,0,0};vg_ids ids={0};int32_t count,dim,attrs,marker=0;bool node,ok=false;uint32_t row;char name[48];if(!vg_utf(b,n,true,pd)||!vg_next(&q)||!vg_i(&q,&count)||count<1||count>4000||!vg_i(&q,&dim)||!vg_i(&q,&attrs)||attrs<0||attrs>64)return false;node=!vg_done(&q);if(node){if((dim!=2&&dim!=3)||!vg_i(&q,&marker)||marker<0||marker>1||!vg_done(&q))return false;}else if(dim!=4&&dim!=10)return false;if(!vg_emit(f,s,node?"node-descriptor.txt":"element-descriptor.txt",0,q.p,n)||!vg_ids_init(&ids,(uint32_t)count))goto done;for(row=0;row<(uint32_t)count;++row){int32_t id,indices[10];unsigned j;double v;if(vg_stop(pd)||!vg_next(&q)||!vg_i(&q,&id)||id<0||id==2147483647||!vg_id(&ids,(uint32_t)id+1,true,pd))goto done;for(j=0;j<(unsigned)dim;++j){if(node){if(!vg_num(&q,&v))goto done;}else {unsigned k;if(!vg_i(&q,&indices[j])||indices[j]<0)goto done;for(k=0;k<j;++k)if(indices[k]==indices[j])goto done;}}for(j=0;j<(unsigned)attrs;++j)if(!vg_num(&q,&v))goto done;if(node&&marker&&(!vg_i(&q,&id)||id<0))goto done;if(!vg_done(&q))goto done;xx_rt_snprintf(name,sizeof(name),node?"node-%u.txt":"element-%u.txt",row);if(!vg_emit(f,s,name,q.start,q.p-q.start,n))goto done;}if(vg_next(&q)||q.p!=n)goto done;ok=vg_cover(f,s,"comments.txt",n);done:if(ids.values)xx_mem_free(ids.values);return ok;}

void xx_tetgen_mesh_init(xx_tetgen_mesh *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_TETGEN_MESH,"node");}}
xx_tetgen_mesh *xx_tetgen_mesh_create(xx_io_device *d,int64_t at) {xx_tetgen_mesh *r=(xx_tetgen_mesh *)xx_mem_alloc(sizeof(*r));if(r)xx_tetgen_mesh_init(r,d,at);return r;}
void xx_tetgen_mesh_destroy(xx_tetgen_mesh *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_tetgen_mesh_free(xx_tetgen_mesh *r) {if(r){xx_tetgen_mesh_destroy(r);xx_mem_free(r);}}
bool xx_tetgen_mesh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_tetgen_mesh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
