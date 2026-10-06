/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.metaseq.net/en/format.html
 * Metasequoia MQO1.0/1.1 ASCII text meshes: complete text vertices and polygon V/M/UV fields, local indexes/material references, finite typed properties, classic material/scene/light and RGB24 raw-hex thumbnail chunks. Original sections exported. NonASCII locale text, CodePage/MaterialEx/binary vertices/other extensions and external loading declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/metasequoia_mqo/xx_metasequoia_mqo.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[20];return n>=20&&pm_read(f,0,b,20)&&tb_tag(b,"Metasequoia Document",19);}
static bool mq_open(tb_text *q) {return tb_word(q,"{")&&tb_done(q);}
static bool mq_property(tb_text *q,bool scene) {
 const char *v3[]={"scale","rotation","translation","color","pos","lookat","amb","dir"};const char *v1[]={"facet","normal_weight","head","pich","bank","zoom2","frontclip","backclip"};const char *integer[]={"depth","folding","visible","locking","shading","color_type","ortho"};unsigned i;int32_t v;
 for(i=0;i<sizeof(v3)/sizeof(v3[0]);++i)if(tb_word(q,v3[i]))return tb_nums(q,3);
 for(i=0;i<sizeof(v1)/sizeof(v1[0]);++i)if(tb_word(q,v1[i]))return tb_nums(q,1);
 for(i=0;i<sizeof(integer)/sizeof(integer[0]);++i) {if(tb_word(q,integer[i]))return tb_i(q,&v)&&v>=0&&v<=65535&&tb_done(q); } (void)scene;return false;
}
static bool mq_args(tb_text *q,char key[32],tb_text *args) {uint64_t a,end;unsigned j=0;tb_space(q);while(q->t<q->stop&&q->b[q->t]!='('){uint8_t c=q->b[q->t++];if(c<65||c>122||j>=31)return false;key[j++]=(char)c;}key[j]=0;if(!j||q->t==q->stop)return false;a=++q->t;end=a;while(end<q->stop&&q->b[end]!=')'){if(q->b[end]=='(')return false;++end;}if(end==q->stop)return false;*args=*q;args->t=a;args->stop=end;q->t=end+1;return true;}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tb_text q={b,0,n,0,0,0};uint64_t start;int32_t materials=0,objects=0,count,i,j,v,nv=0,nf=0;bool mat=false,scene=false,end=false,wide=false;
 if(!tb_utf(b,n,true,pd)||!tb_line(&q)||!tb_word(&q,"Metasequoia Document")||!tb_done(&q)||!tb_line(&q)||!tb_word(&q,"Format Text Ver"))return false;
 if(tb_word(&q,"1.1"))wide=true;else if(!tb_word(&q,"1.0"))return false;if(!tb_done(&q)||!tb_emit(f,s,"descriptor.mqo",0,q.p,n))return false;
 while(q.p<n){start=q.p;if(tb_stop(pd))return false;if(!tb_next(&q)){if(q.p<n)return false;break;}
  if(tb_word(&q,"Eof")){if(!tb_done(&q))return false;while(q.p<n)if(!tb_line(&q)||!tb_done(&q))return false;if(!tb_emit(f,s,"terminator.mqo",start,n-start,n))return false;end=true;break;}
  if(tb_word(&q,"Thumbnail")){int32_t w,h,depth;uint64_t hex=0,need;if(!tb_i(&q,&w)||!tb_i(&q,&h)||w<1||h<1||w>1024||h>1024||!tb_i(&q,&depth)||depth!=24||!tb_word(&q,"rgb raw")||!mq_open(&q))return false;need=(uint64_t)w*h*6;
   for(;;){if(!tb_line(&q)||tb_stop(pd))return false;if(tb_word(&q,"}")){if(!tb_done(&q)||hex!=need)return false;break;}tb_space(&q);while(q.t<q.stop){uint8_t c=b[q.t++];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'))||++hex>need)return false;}}
   if(!tb_emit(f,s,"thumbnail.mqo",start,q.p-start,n))return false;
  }else if(tb_word(&q,"Scene")){if(scene||!mq_open(&q))return false;scene=true;for(;;){if(!tb_next(&q)||tb_stop(pd))return false;if(tb_word(&q,"}")){if(!tb_done(&q))return false;break;}
    if(tb_word(&q,"dirlights")){if(!tb_i(&q,&count)||count<0||count>64||!mq_open(&q))return false;for(i=0;i<count;++i){unsigned flags=0;if(!tb_next(&q)||!tb_word(&q,"light")||!mq_open(&q))return false;for(;;){if(!tb_next(&q))return false;if(tb_word(&q,"}")){if(!tb_done(&q)||flags!=3)return false;break;}if(tb_word(&q,"dir")){if(flags&1)return false;flags|=1;}else if(tb_word(&q,"color")){if(flags&2)return false;flags|=2;}else return false;if(!tb_nums(&q,3))return false;}}if(!tb_next(&q)||!tb_word(&q,"}")||!tb_done(&q))return false;
    }else if(!mq_property(&q,true))return false;
   }if(!tb_emit(f,s,"scene.mqo",start,q.p-start,n))return false;
  }else if(tb_word(&q,"Material")){if(mat||objects||!tb_i(&q,&materials)||materials<0||materials>4096||!mq_open(&q))return false;mat=true;
   for(i=0;i<materials;++i){unsigned flags=0;if(!tb_next(&q)||!tb_string(&q))return false;while(!tb_done(&q)){char key[32];tb_text a;unsigned nums=1;double x;if(!mq_args(&q,key,&a))return false;
     if(!xx_rt_strcmp(key,"col")){if(flags&1)return false;flags|=1;nums=4;}
     else if(!xx_rt_strcmp(key,"shader")||!xx_rt_strcmp(key,"vcol")){if(!tb_i(&a,&v)||v<0||v>4||!tb_done(&a))return false;continue;}
     else if(xx_rt_strcmp(key,"dif")&&xx_rt_strcmp(key,"amb")&&xx_rt_strcmp(key,"emi")&&xx_rt_strcmp(key,"spc")&&xx_rt_strcmp(key,"power")&&xx_rt_strcmp(key,"reflect")&&xx_rt_strcmp(key,"refract"))return false;
     for(j=0;j<(int32_t)nums;++j) {if(!tb_num(&a,&x)||x<0||(nums==4&&x>1))return false; } if(!tb_done(&a))return false;
    }if(!(flags&1))return false;
   }if(!tb_next(&q)||!tb_word(&q,"}")||!tb_done(&q)||!tb_emit(f,s,"materials.mqo",start,q.p-start,n))return false;
  }else if(tb_word(&q,"Object")){unsigned flags=0;if(++objects>1024||!tb_string(&q)||!mq_open(&q))return false;nv=0;nf=0;
   for(;;){if(tb_stop(pd)||!tb_next(&q))return false;if(tb_word(&q,"}")){if(!tb_done(&q)||flags!=3)return false;break;}
    if(tb_word(&q,"vertex")){if(flags&1||!tb_i(&q,&nv)||nv<3||nv>1000000||!mq_open(&q))return false;flags|=1;for(i=0;i<nv;++i)if(tb_stop(pd)||!tb_next(&q)||!tb_nums(&q,3))return false;if(!tb_next(&q)||!tb_word(&q,"}")||!tb_done(&q))return false;}
    else if(tb_word(&q,"face")){if(!(flags&1)||(flags&2)||!tb_i(&q,&nf)||nf<1||nf>1000000||!mq_open(&q))return false;flags|=2;
     for(i=0;i<nf;++i){unsigned ff=0;if(tb_stop(pd)||!tb_next(&q)||!tb_i(&q,&count)||count<2||count>(wide?64:4))return false;while(!tb_done(&q)){char key[32];tb_text a;if(!mq_args(&q,key,&a))return false;
       if(!xx_rt_strcmp(key,"V")){int32_t used[64],k;if(ff&1)return false;ff|=1;for(j=0;j<count;++j){if(!tb_i(&a,&v)||v<0||v>=nv)return false;for(k=0;k<j;++k)if(used[k]==v)return false;used[j]=v;}if(!tb_done(&a))return false;}
       else if(!xx_rt_strcmp(key,"M")){if(ff&2||!tb_i(&a,&v)||v< -1||v>=materials||!tb_done(&a))return false;ff|=2;}
       else if(!xx_rt_strcmp(key,"UV")){if(ff&4||!tb_nums(&a,(unsigned)count*2))return false;ff|=4;}else return false;
      }if(!(ff&1))return false;
     }if(!tb_next(&q)||!tb_word(&q,"}")||!tb_done(&q))return false;
    }else if(!mq_property(&q,false))return false;
   }if(!tb_emit(f,s,"object.mqo",start,q.p-start,n))return false;
  }else return false;
 }if(!end||!objects)return false;s->size=(int64_t)n;return true;
}

void xx_metasequoia_mqo_init(xx_metasequoia_mqo *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_METASEQUOIA_MQO,"mqo");}}
xx_metasequoia_mqo *xx_metasequoia_mqo_create(xx_io_device *d,int64_t at) {xx_metasequoia_mqo *r=(xx_metasequoia_mqo *)xx_mem_alloc(sizeof(*r));if(r)xx_metasequoia_mqo_init(r,d,at);return r;}
void xx_metasequoia_mqo_destroy(xx_metasequoia_mqo *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_metasequoia_mqo_free(xx_metasequoia_mqo *r) {if(r){xx_metasequoia_mqo_destroy(r);xx_mem_free(r);}}
bool xx_metasequoia_mqo_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_metasequoia_mqo_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
