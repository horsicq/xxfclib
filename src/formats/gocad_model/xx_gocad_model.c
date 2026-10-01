/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/lanl/LaGriT/master/src/read_gocad_tsurf.f
 * GOCAD TSurf1 triangulated surfaces: complete header/property declarations and TFACE records with unique local vertex IDs, finite coordinates/properties, resolved ATOM/TRGL/border references and END framing. Original descriptor and typed surface records exported; external coordinate systems/solid/voxel/complex extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/gocad_model/xx_gocad_model.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t b[13];return n>=24&&pm_read(f,0,b,13)&&fg_tag(b,"GOCAD TSurf 1",13);}
static bool fg_gocad_field(fg_text *q,const char *key) {size_t z=xx_rt_strlen(key);fg_space(q);if(!fg_span(q->t,z+1,q->stop)||!fg_tag(q->b+q->t,key,z)||q->b[q->t+z]!=':')return false;q->t+=z+1;return true;}
static bool fg_gocad_words(fg_text *q,unsigned *count) {unsigned n=0;fg_space(q);while(q->t<q->stop){uint64_t at=q->t;while(q->t<q->stop&&q->b[q->t]!=32&&q->b[q->t]!=9){if(q->t-at>=255)return false;++q->t;}if(q->t==at)return false;++n;fg_space(q);}*count=n;return n>0&&n<=64;}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_text q={b,0,n,0,0,0};fg_ids ids={0};unsigned props=0,faces=0,nodes=0,triangles=0;bool header=false,ended=false,ok=false,name=false;uint64_t face=0;char label[48];int32_t value;
 if(!fg_utf(b,n,true,pd)||!fg_next(&q)||!fg_word(&q,"GOCAD")||!fg_word(&q,"TSurf")||!fg_i(&q,&value)||value!=1||!fg_done(&q)||!fg_next(&q)||!fg_word(&q,"HEADER")||!fg_word(&q,"{")||!fg_done(&q))return false;
 while(fg_next(&q)){if(fg_word(&q,"}")){if(!fg_done(&q))return false;header=true;break;}if(fg_gocad_field(&q,"name")){if(name||q.t==q.stop||q.stop-q.t>255)return false;name=true;}else if(fg_gocad_field(&q,"*solid*color")){unsigned i;double v;for(i=0;i<4;++i)if(!fg_num(&q,&v)||v<0||v>1)return false;if(!fg_done(&q))return false;}else return false;}
 if(!header||!name||!fg_ids_init(&ids,1000000))return false;
 while(fg_next(&q)){if(fg_stop(pd))goto done;
 if(fg_word(&q,"GOCAD_ORIGINAL_COORDINATE_SYSTEM")){if(faces||!fg_done(&q)||!fg_next(&q)||!fg_word(&q,"NAME")||q.t==q.stop||!fg_next(&q)||!fg_word(&q,"AXIS_NAME")||!fg_string(&q)||!fg_string(&q)||!fg_string(&q)||!fg_done(&q)||!fg_next(&q)||!fg_word(&q,"AXIS_UNIT")||!fg_string(&q)||!fg_string(&q)||!fg_string(&q)||!fg_done(&q)||!fg_next(&q)||!fg_word(&q,"ZPOSITIVE")||(!fg_word(&q,"Elevation")&&!fg_word(&q,"Depth"))||!fg_done(&q)||!fg_next(&q)||!fg_word(&q,"END_ORIGINAL_COORDINATE_SYSTEM")||!fg_done(&q))goto done;}
 else if(fg_word(&q,"GEOLOGICAL_TYPE")){unsigned words;if(faces||!fg_gocad_words(&q,&words)||words!=1)goto done;}
 else if(fg_word(&q,"PROPERTIES")){if(faces||props||!fg_gocad_words(&q,&props)||!fg_next(&q)||!fg_word(&q,"ESIZES"))goto done;{unsigned i;for(i=0;i<props;++i)if(!fg_i(&q,&value)||value!=1)goto done;}if(!fg_done(&q))goto done;}
 else if(fg_word(&q,"TFACE")){if(!fg_done(&q))goto done;if(face){if(!nodes||!triangles)goto done;xx_rt_snprintf(label,sizeof(label),"surface-%u.ts",faces-1);if(!fg_emit(f,s,label,face,q.start-face,n))goto done;}else if(!fg_emit(f,s,"descriptor.ts",0,q.start,n))goto done;if(++faces>4000)goto done;face=q.start;nodes=triangles=0;}
 else if(fg_word(&q,"VRTX")||fg_word(&q,"PVRTX")){unsigned i,dimensions=3+props;double v;if(!face||!fg_i(&q,&value)||value<1||!fg_id(&ids,(uint32_t)value,true,pd))goto done;for(i=0;i<dimensions;++i)if(!fg_num(&q,&v))goto done;if(!fg_done(&q)||++nodes>1000000)goto done;}
 else if(fg_word(&q,"ATOM")){int32_t target;if(!face||!fg_i(&q,&value)||value<1||!fg_i(&q,&target)||target<1||!fg_done(&q)||!fg_id(&ids,(uint32_t)target,false,pd)||!fg_id(&ids,(uint32_t)value,true,pd)||++nodes>1000000)goto done;}
 else if(fg_word(&q,"TRGL")){int32_t a,c,d;if(!face||!fg_i(&q,&a)||!fg_i(&q,&c)||!fg_i(&q,&d)||a<1||c<1||d<1||a==c||a==d||c==d||!fg_done(&q)||!fg_id(&ids,(uint32_t)a,false,pd)||!fg_id(&ids,(uint32_t)c,false,pd)||!fg_id(&ids,(uint32_t)d,false,pd)||++triangles>1000000)goto done;}
 else if(fg_word(&q,"BSTONE")){if(!face||!fg_i(&q,&value)||value<1||!fg_id(&ids,(uint32_t)value,false,pd)||!fg_done(&q))goto done;}
 else if(fg_word(&q,"BORDER")){int32_t a,c;if(!face||!fg_i(&q,&value)||value<1||!fg_i(&q,&a)||!fg_i(&q,&c)||a<1||c<1||a==c||!fg_id(&ids,(uint32_t)a,false,pd)||!fg_id(&ids,(uint32_t)c,false,pd)||!fg_done(&q))goto done;}
 else if(fg_word(&q,"END")){if(!face||!nodes||!triangles||!fg_done(&q))goto done;xx_rt_snprintf(label,sizeof(label),"surface-%u.ts",faces-1);if(!fg_emit(f,s,label,face,q.start-face,n)||!fg_emit(f,s,"terminator.ts",q.start,q.p-q.start,n))goto done;ended=true;break;}else goto done;
 }
 ok=ended&&!fg_next(&q)&&q.p==q.end&&fg_cover(f,s,"framing.ts",n);
done:xx_mem_free(ids.values);return ok;
}

void xx_gocad_model_init(xx_gocad_model *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_GOCAD_MODEL,"ts");}}
xx_gocad_model *xx_gocad_model_create(xx_io_device *d,int64_t at) {xx_gocad_model *r=(xx_gocad_model *)xx_mem_alloc(sizeof(*r));if(r)xx_gocad_model_init(r,d,at);return r;}
void xx_gocad_model_destroy(xx_gocad_model *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gocad_model_free(xx_gocad_model *r) {if(r){xx_gocad_model_destroy(r);xx_mem_free(r);}}
bool xx_gocad_model_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_gocad_model_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
