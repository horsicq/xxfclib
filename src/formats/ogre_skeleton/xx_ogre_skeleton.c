/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/OGRECave/ogre/master/OgreMain/src/OgreSkeletonSerializer.cpp
 * OGRE skeleton serializer v1.10/v1.80: complete bounded bone/hierarchy/animation track/keyframe chunks with finite transforms, resolved bone handles and acyclic parent links. Original typed chunks exported; external animation links, animation base-keyframe extensions and unknown chunks declined. Bone-name bytes excluded from the historical declared bone chunk length are validated and retained.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/ogre_skeleton/xx_ogre_skeleton.h"
#include "../wbmp_image/xx_fifteenth_games.h"
static bool vg_quick(Abstractformat *f,uint64_t n) {uint8_t b[2];return n>=24&&pm_read(f,0,b,2)&&pm_le16(b)==0x1000;}
static bool vg_ogre_string(vg_bin *q) {uint64_t start=q->p;while(q->p<q->n&&q->b[q->p]!=10){if(q->p-start>=255||q->b[q->p]<32||q->b[q->p]>126)return false;++q->p;}return q->p>start&&q->p<q->n&&++q->p<=q->n;}
static bool vg_ogre_animation(vg_bin *q,uint64_t end,uint32_t bones) {const uint8_t *p;double duration;uint8_t *tracks=NULL;unsigned count=0;bool ok=false;if(!vg_ogre_string(q)||!vg_float(q,&duration)||duration<=0)return false;tracks=(uint8_t *)xx_mem_alloc(bones);if(!tracks)return false;xx_mem_zero(tracks,bones);while(q->p<end){uint64_t start=q->p,tend;unsigned id,bone,keys=0;uint32_t z;double previous=-1;if(!vg_take(q,6,&p)||(id=pm_le16(p))!=0x4100||(z=pm_le32(p+2))<8||!vg_span(start,z,end))goto done;tend=start+z;if(!vg_take(q,2,&p)||(bone=pm_le16(p))>=bones||tracks[bone])goto done;tracks[bone]=1;++count;while(q->p<tend){double time;start=q->p;if(++keys>1000000||!vg_take(q,6,&p)||pm_le16(p)!=0x4110||((z=pm_le32(p+2))!=38&&z!=50)||!vg_span(start,z,tend)||!vg_float(q,&time)||time<0||time>duration||time<=previous||!vg_floats(q,z==38?7:10)||q->p!=start+z)goto done;previous=time;}if(!keys||q->p!=tend)goto done;}ok=count&&q->p==end;
done:xx_mem_free(tracks);return ok;}
static bool vg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {vg_bin q={b,2,n,pd};const uint8_t *p;uint32_t bones=0,*parents=NULL;unsigned chunks=0;bool ok=false,linked=false,blend=false,animations=false;char name[48];if(n<24||pm_le16(b)!=0x1000||!vg_ogre_string(&q)||q.p!=21||(!vg_tag(b+2,"[Serializer_v1.10]",18)&&!vg_tag(b+2,"[Serializer_v1.80]",18)))return false;parents=(uint32_t *)xx_mem_alloc(4000*4);if(!parents||!vg_emit(f,s,"descriptor.skeleton",0,q.p,n))goto done;while(q.p<n){uint64_t start=q.p,end;unsigned kind;uint32_t z;if(!vg_take(&q,6,&p)||(z=pm_le32(p+2))<6)goto done;kind=pm_le16(p);if(kind==0x2000){uint64_t str=q.p;unsigned id;if(linked||animations||bones>=4000||(z!=36&&z!=48)||!vg_ogre_string(&q)||!vg_span(start,z+(q.p-str),n)||!vg_take(&q,2,&p)||(id=pm_le16(p))!=bones||!vg_floats(&q,z==36?7:10))goto done;parents[bones++]=0xffffffffU;end=start+z+(q.p-str-(z-6));if(q.p!=end)goto done;}
 else {if(!vg_span(start,z,n))goto done;end=start+z;if(kind==0x1010){if(blend||bones||linked||animations||z!=8||!vg_take(&q,2,&p)||pm_le16(p)>1)goto done;blend=true;}
 else if(kind==0x3000){unsigned child,parent;linked=true;if(animations||!bones||z!=10||!vg_take(&q,4,&p)||(child=pm_le16(p))>=bones||(parent=pm_le16(p+2))>=bones||child==parent||parents[child]!=0xffffffffU)goto done;parents[child]=parent;}
 else if(kind==0x4000){animations=true;if(!bones||!vg_ogre_animation(&q,end,bones))goto done;}else goto done;if(q.p!=end)goto done;}
 xx_rt_snprintf(name,sizeof(name),"chunk-%u.skeleton",chunks++);if(!vg_emit(f,s,name,start,q.p-start,n))goto done;}
 {uint32_t i,j;for(i=0;i<bones;++i){uint32_t pbone=i;for(j=0;j<bones&&parents[pbone]!=0xffffffffU;++j){if(vg_stop(pd))goto done;pbone=parents[pbone];}if(j==bones)goto done;}}ok=bones&&q.p==n;
done:if(parents)xx_mem_free(parents);return ok;}

void xx_ogre_skeleton_init(xx_ogre_skeleton *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_OGRE_SKELETON,"skeleton");}}
xx_ogre_skeleton *xx_ogre_skeleton_create(xx_io_device *d,int64_t at) {xx_ogre_skeleton *r=(xx_ogre_skeleton *)xx_mem_alloc(sizeof(*r));if(r)xx_ogre_skeleton_init(r,d,at);return r;}
void xx_ogre_skeleton_destroy(xx_ogre_skeleton *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_ogre_skeleton_free(xx_ogre_skeleton *r) {if(r){xx_ogre_skeleton_destroy(r);xx_mem_free(r);}}
bool xx_ogre_skeleton_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_ogre_skeleton_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
