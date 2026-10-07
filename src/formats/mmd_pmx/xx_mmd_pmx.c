/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/MMD-Blender/blender_mmd_tools/main/mmd_tools/core/pmx/__init__.py
 * PMX2.0 model containers: complete bounded typed vertices/skinning/faces/textures/materials/bones/morphs/display/rigid-body/joint arrays with valid local indexes. Original model sections exported; no external asset loading, simulation or rendering.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/mmd_pmx/xx_mmd_pmx.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[9];return n>=17&&pm_read(f,0,b,9)&&tb_tag(b,"PMX ",4)&&xx_data_get_u32(b+4, 4, 0, false)==0x40000000U&&b[8]==8;}
static bool mx_text(tb_bin *q,bool utf8) {
 uint32_t z;const uint8_t *b;unsigned i;if(!tb_count(q,1048576,&z)||!tb_take(q,z,&b))return false;if(utf8)return tb_utf(b,z,false,q->pd);if(z&1)return false;
 for(i=0;i<z;i+=2){uint16_t c=xx_data_get_u16(b+i, 2, 0, false);if(!c)return false;if(c>=0xd800&&c<=0xdbff){if(i+3>=z||(c=xx_data_get_u16(b+i+2, 2, 0, false))<0xdc00||c>0xdfff)return false;i+=2;}else if(c>=0xdc00&&c<=0xdfff)return false;}return true;
}
static bool mx_ref(tb_bin *q,unsigned size,uint32_t limit,bool nullable,int32_t *value) {return tb_index(q,size,false,value)&&(*value==-1?nullable:(uint32_t)*value<limit);}
static bool mx_bone(tb_bin *q,unsigned size,int32_t *maximum,int32_t *value) {if(!tb_index(q,size,false,value))return false;if(*value>*maximum)*maximum=*value;return true;}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tb_bin q={b,17,n,pd};unsigned sz[6],i,j;uint32_t vertices,indices,textures,materials,bones,morphs,displays,rigids,joints,aux;uint64_t start,covered=0;int32_t maximum=-1,mmorph=-1,index,*parents=NULL;uint8_t *marks=NULL;bool utf8,result=false;
 const uint8_t *p;double value;
#define MX(x) do{if(!(x))goto done;}while(0)
 MX(tb_tag(b,"PMX ",4)&&xx_data_get_u32(b+4, 4, 0, false)==0x40000000U&&b[8]==8&&b[9]<=1&&b[10]<=4);utf8=b[9]!=0;
 for(i=0;i<6;++i){sz[i]=b[11+i];MX(sz[i]==1||sz[i]==2||sz[i]==4);}
 for(i=0;i<4;++i) {MX(mx_text(&q,utf8)); } MX(tb_emit(f,s,"descriptor.pmx",0,q.p,n));
 start=q.p;MX(tb_count(&q,1000000,&vertices)&&vertices>=3);
 for(i=0;i<vertices;++i){uint8_t weight;unsigned amount;MX(tb_floats(&q,8+b[10]*4)&&tb_take(&q,1,&p));weight=p[0];MX(weight<=3);amount=weight==0?1:weight==2?4:2;
  for(j=0;j<amount;++j)MX(mx_bone(&q,sz[3],&maximum,&index));
  if(weight==1||weight==3){MX(tb_float(&q,&value)&&value>=0&&value<=1);}
  else if(weight==2){double sum=0;for(j=0;j<4;++j){MX(tb_float(&q,&value)&&value>=0&&value<=1);sum+=value;}MX(sum>=0.999&&sum<=1.001);}
  if(weight==3) {MX(tb_floats(&q,9)); } MX(tb_float(&q,&value)&&value>=0);
 }MX(tb_emit(f,s,"vertices.pmx",start,q.p-start,n));
 start=q.p;MX(tb_count(&q,3000000,&indices)&&indices&&indices%3==0);
 for(i=0;i<indices;++i){MX(tb_index(&q,sz[0],true,&index)&&(uint32_t)index<vertices);}MX(tb_emit(f,s,"triangles.pmx",start,q.p-start,n));
 start=q.p;MX(tb_count(&q,65536,&textures));for(i=0;i<textures;++i)MX(mx_text(&q,utf8));MX(tb_emit(f,s,"textures.pmx",start,q.p-start,n));
 start=q.p;MX(tb_count(&q,65536,&materials)&&materials);covered=0;
 for(i=0;i<materials;++i){uint8_t flags,toon;MX(mx_text(&q,utf8)&&mx_text(&q,utf8)&&tb_floats(&q,11)&&tb_take(&q,1,&p));flags=p[0];MX(!(flags&~31U)&&tb_floats(&q,5)&&mx_ref(&q,sz[1],textures,true,&index)&&mx_ref(&q,sz[1],textures,true,&index)&&tb_take(&q,2,&p)&&p[0]<=3&&p[1]<=1);toon=p[1];
  if(toon){MX(tb_take(&q,1,&p)&&p[0]<=9);}else MX(mx_ref(&q,sz[1],textures,true,&index));
  MX(mx_text(&q,utf8)&&tb_count(&q,indices,&aux)&&aux%3==0);covered+=aux;MX(covered<=indices);
 }MX(covered==indices&&tb_emit(f,s,"materials.pmx",start,q.p-start,n));
 start=q.p;MX(tb_count(&q,65536,&bones));MX(maximum<0||(uint32_t)maximum<bones);
 parents=(int32_t *)xx_mem_alloc((bones?bones:1)*sizeof(*parents));marks=(uint8_t *)xx_mem_alloc(bones?bones:1);MX(parents&&marks);xx_mem_zero(marks,bones);
 for(i=0;i<bones;++i){uint16_t flags;MX(mx_text(&q,utf8)&&mx_text(&q,utf8)&&tb_floats(&q,3)&&mx_ref(&q,sz[3],bones,true,&parents[i])&&parents[i]!=(int32_t)i&&tb_count(&q,1000000,&aux)&&tb_take(&q,2,&p));flags=xx_data_get_u16(p, 2, 0, false);MX(!(flags&~0x3f3fU));
  if(flags&1)MX(mx_ref(&q,sz[3],bones,true,&index));else MX(tb_floats(&q,3));
  if(flags&0x300)MX(mx_ref(&q,sz[3],bones,true,&index)&&tb_floats(&q,1));
  if(flags&0x400) {MX(tb_floats(&q,3)); } if(flags&0x800)MX(tb_floats(&q,6));if(flags&0x2000)MX(tb_take(&q,4,NULL));
  if(flags&0x20){uint32_t links;MX(mx_ref(&q,sz[3],bones,false,&index)&&tb_count(&q,100000,&aux)&&tb_float(&q,&value)&&value>=0&&value<=3.142&&tb_count(&q,4096,&links));
   for(j=0;j<links;++j){MX(mx_ref(&q,sz[3],bones,false,&index)&&tb_take(&q,1,&p)&&p[0]<=1);if(p[0])MX(tb_floats(&q,6));}
  }
 }
 for(i=0;i<bones;++i){int32_t at=(int32_t)i;while(at>=0&&!marks[at]){marks[at]=1;at=parents[at];}MX(at<0||marks[at]!=1);at=(int32_t)i;while(at>=0&&marks[at]==1){marks[at]=2;at=parents[at];}}
 MX(tb_emit(f,s,"bones.pmx",start,q.p-start,n));
 start=q.p;MX(tb_count(&q,65536,&morphs));
 for(i=0;i<morphs;++i){uint8_t type;uint32_t count;MX(mx_text(&q,utf8)&&mx_text(&q,utf8)&&tb_take(&q,2,&p)&&p[0]<=4&&p[1]<=8);type=p[1];MX(type<=3||type==8||(unsigned)(type-3)<=b[10]);MX(tb_count(&q,1000000,&count));
  for(j=0;j<count;++j){
   if(type==0){MX(tb_index(&q,sz[4],false,&index)&&index>=0&&index!=(int32_t)i&&tb_floats(&q,1));if(index>mmorph)mmorph=index;}
   else if(type==1||(type>=3&&type<=7)){MX(tb_index(&q,sz[0],true,&index)&&(uint32_t)index<vertices&&tb_floats(&q,type==1?3:4));}
   else if(type==2)MX(mx_ref(&q,sz[3],bones,false,&index)&&tb_floats(&q,7));
   else MX(mx_ref(&q,sz[2],materials,true,&index)&&tb_take(&q,1,&p)&&p[0]<=1&&tb_floats(&q,28));
  }
 }MX(mmorph<0||(uint32_t)mmorph<morphs);MX(tb_emit(f,s,"morphs.pmx",start,q.p-start,n));
 start=q.p;MX(tb_count(&q,65536,&displays));for(i=0;i<displays;++i){uint32_t count;MX(mx_text(&q,utf8)&&mx_text(&q,utf8)&&tb_take(&q,1,&p)&&p[0]<=1&&tb_count(&q,1000000,&count));for(j=0;j<count;++j){uint8_t type;MX(tb_take(&q,1,&p)&&p[0]<=1);type=p[0];MX(mx_ref(&q,sz[type?4:3],type?morphs:bones,false,&index));}}MX(tb_emit(f,s,"display.pmx",start,q.p-start,n));
 start=q.p;MX(tb_count(&q,65536,&rigids));for(i=0;i<rigids;++i){MX(mx_text(&q,utf8)&&mx_text(&q,utf8)&&mx_ref(&q,sz[3],bones,true,&index)&&tb_take(&q,4,&p)&&p[0]<=15&&p[3]<=2&&tb_floats(&q,14)&&tb_take(&q,1,&p)&&p[0]<=2);}MX(tb_emit(f,s,"rigid-bodies.pmx",start,q.p-start,n));
 start=q.p;MX(tb_count(&q,65536,&joints));for(i=0;i<joints;++i){MX(mx_text(&q,utf8)&&mx_text(&q,utf8)&&tb_take(&q,1,&p)&&p[0]==0&&mx_ref(&q,sz[5],rigids,true,&index)&&mx_ref(&q,sz[5],rigids,true,&index)&&tb_floats(&q,24));}MX(q.p==n&&tb_emit(f,s,"joints.pmx",start,q.p-start,n));s->size=(int64_t)n;result=true;
done:xx_mem_free(parents);xx_mem_free(marks);return result;
#undef MX
}

void xx_mmd_pmx_init(xx_mmd_pmx *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_MMD_PMX,"pmx");}}
xx_mmd_pmx *xx_mmd_pmx_create(xx_io_device *d,int64_t at) {xx_mmd_pmx *r=(xx_mmd_pmx *)xx_mem_alloc(sizeof(*r));if(r)xx_mmd_pmx_init(r,d,at);return r;}
void xx_mmd_pmx_destroy(xx_mmd_pmx *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_mmd_pmx_free(xx_mmd_pmx *r) {if(r){xx_mmd_pmx_destroy(r);xx_mem_free(r);}}
bool xx_mmd_pmx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_mmd_pmx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
