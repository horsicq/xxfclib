/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/BVH/BVHLoader.cpp
 * Biovision BVH: complete single-root hierarchy with bounded joints/end sites, unique channels, finite offsets and exact counted finite motion frames. Original hierarchy and motion table exported; multiple roots and nonstandard channels declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/bvh_motion/xx_bvh_motion.h"
#include "../fontforge_sfd/xx_fourteenth_games.h"
static bool fg_quick(Abstractformat *f,uint64_t n) {uint8_t b[9];return n>=24&&pm_read(f,0,b,9)&&fg_tag(b,"HIERARCHY",9);}
static bool fg_bvh_node(fg_lex *q,unsigned depth,unsigned *joints,unsigned *channels,bool root,bool site) {
 uint64_t at,z;int32_t count;double v;unsigned i,mask=0;bool child=false;
 if(depth>64||++*joints>4096)return false;
 if(site){if(!fg_kw(q,"End")||!fg_kw(q,"Site"))return false;}
 else if(!(root?fg_kw(q,"ROOT"):fg_kw(q,"JOINT"))||!fg_ident(q,&at,&z))return false;
 if(!fg_char(q,'{')||!fg_kw(q,"OFFSET")||!fg_number(q,&v)||!fg_number(q,&v)||!fg_number(q,&v))return false;
 if(!site){if(!fg_kw(q,"CHANNELS")||!fg_integer(q,&count)||count<1||count>6)return false;for(i=0;i<(unsigned)count;++i){unsigned bit;if(fg_kw(q,"Xposition"))bit=1;else if(fg_kw(q,"Yposition"))bit=2;else if(fg_kw(q,"Zposition"))bit=4;else if(fg_kw(q,"Xrotation"))bit=8;else if(fg_kw(q,"Yrotation"))bit=16;else if(fg_kw(q,"Zrotation"))bit=32;else return false;if(mask&bit)return false;mask|=bit;}*channels+=(unsigned)count;if(*channels>24576)return false;
 while(fg_skip(q)&&q->p<q->n&&q->b[q->p]!='}'){fg_lex save=*q;bool end=fg_kw(q,"End");*q=save;if(!fg_bvh_node(q,depth+1,joints,channels,false,end))return false;child=true;}if(!child)return false;}
 return fg_char(q,'}');
}
static bool fg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 fg_lex q={b,0,n,pd,0,false,false,false};unsigned joints=0,channels=0;int32_t frames;double dt,v;uint64_t p,i,total;
 if(!fg_utf(b,n,true,pd)||!fg_kw(&q,"HIERARCHY")||!fg_bvh_node(&q,0,&joints,&channels,true,false)||!fg_emit(f,s,"hierarchy.bvh",0,q.p,n))return false;p=q.p;
 if(!fg_kw(&q,"MOTION")||!fg_kw(&q,"Frames")||!fg_char(&q,':')||!fg_integer(&q,&frames)||frames<1||frames>1000000||!fg_kw(&q,"Frame")||!fg_kw(&q,"Time")||!fg_char(&q,':')||!fg_number(&q,&dt)||dt<=0||dt>60||!fg_emit(f,s,"motion-descriptor.bvh",p,q.p-p,n))return false;p=q.p;total=(uint64_t)frames*channels;if(total>16000000)return false;
 for(i=0;i<total;++i)if(!fg_number(&q,&v))return false;
 return fg_emit(f,s,"motion-frames.bvh",p,q.p-p,n)&&fg_end(&q)&&fg_cover(f,s,"whitespace.bvh",n);
}

void xx_bvh_motion_init(xx_bvh_motion *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_BVH_MOTION,"bvh");}}
xx_bvh_motion *xx_bvh_motion_create(xx_io_device *d,int64_t at) {xx_bvh_motion *r=(xx_bvh_motion *)xx_mem_alloc(sizeof(*r));if(r)xx_bvh_motion_init(r,d,at);return r;}
void xx_bvh_motion_destroy(xx_bvh_motion *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_bvh_motion_free(xx_bvh_motion *r) {if(r){xx_bvh_motion_destroy(r);xx_mem_free(r);}}
bool xx_bvh_motion_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_bvh_motion_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
