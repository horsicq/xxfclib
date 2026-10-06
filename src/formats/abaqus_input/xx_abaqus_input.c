/* SPDX-License-Identifier: MIT
 * Primary reference: https://docs.software.vt.edu/abaqusv2025/English/SIMACAEMODRefMap/simamod-c-inputsyntax.htm
 * Abaqus mesh input subset: complete typed NODE/ELEMENT/ELSET/NSET/SURFACE blocks, finite coordinates, unique local identifiers and resolved connectivity/set references. Original typed mesh blocks and comments exported; includes, solver steps, material evaluation and arbitrary keywords declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/abaqus_input/xx_abaqus_input.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t c;return n>=24&&pm_read(f,0,&c,1)&&(c=='*'||c==32);}
static bool fg_ab_key(fg_text *q,const char *s) {size_t z=xx_rt_strlen(s),i;fg_space(q);if(!fg_span(q->t,z,q->stop))return false;for(i=0;i<z;++i){uint8_t c=q->b[q->t+i],v=(uint8_t)s[i];if(c>='a'&&c<='z')c-=32;if(v>='a'&&v<='z')v-=32;if(c!=v)return false;}if(q->t+z<q->stop&&q->b[q->t+z]!=','&&q->b[q->t+z]!=32&&q->b[q->t+z]!=9&&q->b[q->t+z]!='=')return false;q->t+=z;return true;}
static bool fg_ab_punct(fg_text *q,uint8_t c) {fg_space(q);if(q->t==q->stop||q->b[q->t++]!=c)return false;return true;}
static bool fg_ab_name(fg_text *q) {uint64_t start;fg_space(q);start=q->t;while(q->t<q->stop&&q->b[q->t]!=','&&q->b[q->t]!=32&&q->b[q->t]!=9){uint8_t c=q->b[q->t++];if(q->t-start>255||(!fg_ident_char(c)&&c!='.'))return false;}return q->t>start;}
static bool fg_ab_csv(fg_text *q,int32_t *v,bool more) {fg_space(q);if(!fg_i(q,v))return false;fg_space(q);if(more)return fg_ab_punct(q,',');if(q->t<q->stop&&q->b[q->t]==',')++q->t;return fg_done(q);}
static bool fg_ab_real(fg_text *q,double *v,bool more) {uint64_t stop;fg_text t;fg_space(q);stop=q->t;while(stop<q->stop&&q->b[stop]!=','&&q->b[stop]!=32&&q->b[stop]!=9)++stop;t=*q;t.stop=stop;if(!fg_num(&t,v)||t.t!=stop)return false;q->t=stop;fg_space(q);return more?fg_ab_punct(q,','):fg_done(q);}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_text q={b,0,n,0,0,0};fg_ids nodes={0},elements={0};uint32_t nodecount=0,elementcount=0;unsigned mode=0,arity=0,block=0,rows=0;uint64_t start=0;bool ok=false,active=false;char label[48];
 if(!fg_utf(b,n,true,pd)||!fg_ids_init(&nodes,1000000)||!fg_ids_init(&elements,1000000))goto done;
 while(fg_line(&q)){int32_t id,index;unsigned i;double v;fg_space(&q);if(fg_stop(pd))goto done;if(q.t==q.stop||(fg_span(q.t,2,q.stop)&&b[q.t]=='*'&&b[q.t+1]=='*'))continue;
 if(b[q.t]=='*'){if(active){if(mode!=1&&mode!=2&&mode!=7&&!rows)goto done;xx_rt_snprintf(label,sizeof(label),"block-%u.inp",block++);if(!fg_emit(f,s,label,start,q.start-start,n))goto done;}start=q.start;active=true;rows=0;++q.t;
 if(fg_ab_key(&q,"Heading")){mode=1;if(!fg_done(&q))goto done;}
 else if(fg_ab_key(&q,"Preprint")){mode=2;while(q.t<q.stop){if(!fg_ab_punct(&q,',')||(!fg_ab_key(&q,"echo")&&!fg_ab_key(&q,"model")&&!fg_ab_key(&q,"history")&&!fg_ab_key(&q,"contact"))||!fg_ab_punct(&q,'=')||(!fg_ab_key(&q,"YES")&&!fg_ab_key(&q,"NO")))goto done;}if(!fg_done(&q))goto done;}
 else if(fg_ab_key(&q,"Node")){mode=3;if(!fg_done(&q))goto done;}
 else if(fg_ab_key(&q,"Element")){mode=4;if(!fg_ab_punct(&q,',')||!fg_ab_key(&q,"type")||!fg_ab_punct(&q,'='))goto done;if(fg_ab_key(&q,"C3D4"))arity=4;else if(fg_ab_key(&q,"C3D8"))arity=8;else if(fg_ab_key(&q,"C3D6"))arity=6;else if(fg_ab_key(&q,"S3"))arity=3;else if(fg_ab_key(&q,"S4"))arity=4;else goto done;if(!fg_done(&q))goto done;}
 else if(fg_ab_key(&q,"Elset")||fg_ab_key(&q,"Nset")){bool el=fg_tag(b+q.start+1,"Elset",5)||fg_tag(b+q.start+1,"ELSET",5);mode=el?5:6;if(!fg_ab_punct(&q,',')||!fg_ab_key(&q,el?"elset":"nset")||!fg_ab_punct(&q,'=')||!fg_ab_name(&q)||!fg_done(&q))goto done;}
 else if(fg_ab_key(&q,"Surface")){mode=8;if(!fg_ab_punct(&q,',')||!fg_ab_key(&q,"type")||!fg_ab_punct(&q,'=')||!fg_ab_key(&q,"ELEMENT")||!fg_ab_punct(&q,',')||!fg_ab_key(&q,"name")||!fg_ab_punct(&q,'=')||!fg_ab_name(&q)||!fg_done(&q))goto done;}
 else if(fg_ab_key(&q,"System")){mode=7;if(!fg_done(&q))goto done;}else goto done;
 }
 else {if(++rows>1000000||!mode||mode==2||mode==7)goto done;
 if(mode==1){if(q.stop-q.t>1024)goto done;}
 else if(mode==3){if(!fg_ab_csv(&q,&id,true)||id<1||!fg_id(&nodes,(uint32_t)id,true,pd)||!fg_ab_real(&q,&v,true)||!fg_ab_real(&q,&v,true)||!fg_ab_real(&q,&v,false)||++nodecount>1000000)goto done;}
 else if(mode==4){if(!fg_ab_csv(&q,&id,true)||id<1||!fg_id(&elements,(uint32_t)id,true,pd)||++elementcount>1000000)goto done;for(i=0;i<arity;++i)if(!fg_ab_csv(&q,&index,i+1<arity)||index<1||!fg_id(&nodes,(uint32_t)index,false,pd))goto done;}
 else if(mode==5||mode==6){do {if(!fg_i(&q,&index)||index<1||!fg_id(mode==5?&elements:&nodes,(uint32_t)index,false,pd))goto done;fg_space(&q);if(q.t==q.stop)break;if(!fg_ab_punct(&q,','))goto done;fg_space(&q);}while(q.t<q.stop);}
 else if(mode==8){if(!fg_ab_csv(&q,&index,true)||index<1||!fg_id(&elements,(uint32_t)index,false,pd)||!fg_ab_punct(&q,'S')||!fg_i(&q,&id)||id<1||id>6||!fg_done(&q))goto done;}
 }
 }
 if(q.p!=q.end||!active||nodecount<3||!elementcount||(mode!=1&&mode!=2&&mode!=7&&!rows)) {goto done; } xx_rt_snprintf(label,sizeof(label),"block-%u.inp",block);if(!fg_emit(f,s,label,start,n-start,n)||!fg_cover(f,s,"framing.inp",n))goto done;ok=true;
done:if(nodes.values)xx_mem_free(nodes.values);if(elements.values)xx_mem_free(elements.values);return ok;
}

void xx_abaqus_input_init(xx_abaqus_input *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_ABAQUS_INPUT,"inp");}}
xx_abaqus_input *xx_abaqus_input_create(xx_io_device *d,int64_t at) {xx_abaqus_input *r=(xx_abaqus_input *)xx_mem_alloc(sizeof(*r));if(r)xx_abaqus_input_init(r,d,at);return r;}
void xx_abaqus_input_destroy(xx_abaqus_input *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_abaqus_input_free(xx_abaqus_input *r) {if(r){xx_abaqus_input_destroy(r);xx_mem_free(r);}}
bool xx_abaqus_input_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_abaqus_input_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
