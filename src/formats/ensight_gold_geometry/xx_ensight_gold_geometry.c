/* SPDX-License-Identifier: MIT
 * Primary reference: https://ansyshelp.ansys.com/public/Views/Secured/corp/v261/en/pdf/Ansys_EnSight_User_Manual.pdf
 * EnSight Gold C-binary little-endian unstructured geometry: complete header, explicit part IDs, unique nonnegative signed32-bit given node/element IDs, finite coordinate tables and typed fixed-arity connectivity with bounded 1-based local indexes. Original descriptor/part coordinate and connectivity tables exported; Fortran binary/ASCII/structured/polygon/time blocks declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/ensight_gold_geometry/xx_ensight_gold_geometry.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t b[8];return n>=400&&pm_read(f,0,b,8)&&fg_tag(b,"C Binary",8);}
static bool fg_ens_text(const uint8_t *p,const char *tag) {unsigned i=0,z=(unsigned)xx_rt_strlen(tag);if(z>80||!fg_tag(p,tag,z))return false;for(i=z;i<80;++i)if(p[i]&&p[i]!=32&&p[i]!=10&&p[i]!=13)return false;return true;}
static bool fg_ens_description(const uint8_t *p) {unsigned i;bool zero=false;for(i=0;i<80;++i){if(!p[i])zero=true;else if(zero||p[i]<32||p[i]>126)return false;}return true;}
static bool fg_ens_ids(fg_bin *q,uint32_t count,fg_ids *set) {const uint8_t *p;uint32_t i,id;for(i=0;i<count;++i)if(!fg_take(q,4,&p)||(id=pm_le32(p))>2147483647U||!fg_id(set,id+1,true,q->pd))return false;return true;}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_bin q={b,400,n,pd};fg_ids parts={0};bool nodeids=false,elemids=false,ok=false;unsigned number=0;const uint8_t *p;char label[64];
 if(n<400||!fg_ens_text(b,"C Binary")||!fg_ens_description(b+80)||!fg_ens_description(b+160))return false;
 if(fg_ens_text(b+240,"node id given"))nodeids=true;else if(!fg_ens_text(b+240,"node id assign")&&!fg_ens_text(b+240,"node id off"))return false;
 if(fg_ens_text(b+320,"element id given"))elemids=true;else if(!fg_ens_text(b+320,"element id assign")&&!fg_ens_text(b+320,"element id off"))return false;
 if(!fg_emit(f,s,"descriptor.geo",0,400,n)||!fg_ids_init(&parts,4000))return false;
 while(q.p<n){uint64_t start=q.p;uint32_t id,nodes,elements=0;fg_ids pointset={0},elementset={0};bool partok=false;unsigned typeindex=0;uint32_t types=0;
 if(++number>1000||!fg_take(&q,80,&p)||!fg_ens_text(p,"part")||!fg_take(&q,4,&p)||!fg_id(&parts,id=pm_le32(p),true,pd)||!fg_take(&q,80,&p)||!fg_ens_description(p)||!fg_take(&q,80,&p)||!fg_ens_text(p,"coordinates")||!fg_count(&q,1000000,&nodes)||!nodes)goto done;
 if(nodeids){if(!fg_ids_init(&pointset,nodes)||!fg_ens_ids(&q,nodes,&pointset))goto part_done;}
 if(!fg_floats(&q,nodes*3))goto part_done;
 xx_rt_snprintf(label,sizeof(label),"part-%u-coordinates.geo",id);if(!fg_emit(f,s,label,start,q.p-start,n))goto part_done;
 if(elemids&&!fg_ids_init(&elementset,1000000))goto part_done;
 while(q.p<n){uint32_t count,i;unsigned arity=0,type=0;start=q.p;if(!fg_span(q.p,80,n))goto part_done;if(fg_ens_text(b+q.p,"part"))break;
 if(!fg_take(&q,80,&p))goto part_done;
 if(fg_ens_text(p,"point")){type=1;arity=1;}else if(fg_ens_text(p,"bar2")){type=2;arity=2;}else if(fg_ens_text(p,"tria3")){type=3;arity=3;}else if(fg_ens_text(p,"quad4")){type=4;arity=4;}else if(fg_ens_text(p,"tetra4")){type=5;arity=4;}else if(fg_ens_text(p,"pyramid5")){type=6;arity=5;}else if(fg_ens_text(p,"penta6")){type=7;arity=6;}else if(fg_ens_text(p,"hexa8")){type=8;arity=8;}else goto part_done;
 if(types&(1U<<type))goto part_done;types|=1U<<type;
 if(!fg_count(&q,1000000,&count)||!count||count>1000000-elements)goto part_done;elements+=count;
 if(elemids&&!fg_ens_ids(&q,count,&elementset))goto part_done;
 for(i=0;i<count*arity;++i){uint32_t index;if(!fg_take(&q,4,&p)||(index=pm_le32(p))<1||index>nodes)goto part_done;}
 xx_rt_snprintf(label,sizeof(label),"part-%u-elements-%u.geo",id,typeindex++);if(!fg_emit(f,s,label,start,q.p-start,n))goto part_done;
 }
 partok=elements>0;
part_done:if(pointset.values)xx_mem_free(pointset.values);if(elementset.values)xx_mem_free(elementset.values);if(!partok)goto done;
 }
 ok=number&&q.p==n;
done:xx_mem_free(parts.values);return ok;
}

void xx_ensight_gold_geometry_init(xx_ensight_gold_geometry *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_ENSIGHT_GOLD_GEOMETRY,"geo");}}
xx_ensight_gold_geometry *xx_ensight_gold_geometry_create(xx_io_device *d,int64_t at) {xx_ensight_gold_geometry *r=(xx_ensight_gold_geometry *)xx_mem_alloc(sizeof(*r));if(r)xx_ensight_gold_geometry_init(r,d,at);return r;}
void xx_ensight_gold_geometry_destroy(xx_ensight_gold_geometry *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ensight_gold_geometry_free(xx_ensight_gold_geometry *r) {if(r){xx_ensight_gold_geometry_destroy(r);xx_mem_free(r);}}
bool xx_ensight_gold_geometry_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_ensight_gold_geometry_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
