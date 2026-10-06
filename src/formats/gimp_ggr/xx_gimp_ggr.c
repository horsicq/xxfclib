/* SPDX-License-Identifier: MIT
 * Primary reference: https://developer.gimp.org/core/standards/ggr/
 * GIMP GGR complete ordered color segments spanning0..1 with finite RGBA values and typed blend/color/endpoint enums. Original segments exported; interpolation/rendering unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/gimp_ggr/xx_gimp_ggr.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[13];return n>=13&&pm_read(f,0,b,13)&&tb_tag(b,"GIMP Gradient",13);}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tb_text q={b,0,n,0,0,0};int32_t count,i;double last=0;uint64_t start;char label[48];
 if(!tb_utf(b,n,false,pd)||!tb_line(&q)||!tb_word(&q,"GIMP")||!tb_word(&q,"Gradient")||!tb_done(&q)||!tb_line(&q))return false;
 if(tb_word(&q,"Name:")){tb_space(&q);if(q.t==q.stop||q.stop-q.t>4096||!tb_line(&q))return false;}
 if(!tb_i(&q,&count)||count<1||count>4093||!tb_done(&q)||!tb_emit(f,s,"descriptor.ggr",0,q.p,n))return false;
 for(i=0;i<count;++i){double x[11];int32_t modes[4]={0,0,0,0};unsigned j;if(tb_stop(pd)||!tb_line(&q))return false;start=q.start;
  for(j=0;j<11;++j)if(!tb_num(&q,&x[j])||x[j]<0||x[j]>1)return false;
  if(x[0]!=last||x[0]>=x[2]||x[1]<x[0]||x[1]>x[2]) {return false; } last=x[2];
  if(!tb_i(&q,&modes[0])||!tb_i(&q,&modes[1])||modes[0]<0||modes[0]>5||modes[1]<0||modes[1]>2)return false;
  if(!tb_done(&q)){if(!tb_i(&q,&modes[2])||!tb_i(&q,&modes[3])||modes[2]<0||modes[2]>4||modes[3]<0||modes[3]>4)return false;}
  if(!tb_done(&q)) {return false; } xx_rt_snprintf(label,sizeof(label),"segment-%u.ggr",(unsigned)i);if(!tb_emit(f,s,label,start,q.p-start,n))return false;
 }
 start=q.p;while(q.p<n){if(!tb_line(&q)||!tb_done(&q))return false;}
 if(last!=1||(q.p>start&&!tb_emit(f,s,"trailing.ggr",start,q.p-start,n))) {return false; } s->size=(int64_t)n;return true;
}

void xx_gimp_ggr_init(xx_gimp_ggr *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_GIMP_GGR,"ggr");}}
xx_gimp_ggr *xx_gimp_ggr_create(xx_io_device *d,int64_t at) {xx_gimp_ggr *r=(xx_gimp_ggr *)xx_mem_alloc(sizeof(*r));if(r)xx_gimp_ggr_init(r,d,at);return r;}
void xx_gimp_ggr_destroy(xx_gimp_ggr *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_gimp_ggr_free(xx_gimp_ggr *r) {if(r){xx_gimp_ggr_destroy(r);xx_mem_free(r);}}
bool xx_gimp_ggr_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_gimp_ggr_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
