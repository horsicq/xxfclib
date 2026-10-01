/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/NGSolve/netgen/master/libsrc/meshing/meshclass.cpp
 * Netgen VOL ASCII complete bounded dimension/point/surface/volume/edge mesh sections with finite coordinates and valid local indexes. Original mesh sections exported; unsupported extensions/higher-order layouts declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/netgen_vol/xx_netgen_vol.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[6];return n>=8&&pm_read(f,0,b,6)&&tb_tag(b,"mesh3d",6);}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 tb_text q={b,0,n,0,0,0};uint32_t seen=0,np=0,maxpoint=0,surfaces=0,volumes=0;int32_t count,i,j,dimension=0;uint64_t start;char label[48];
 if(!tb_utf(b,n,false,pd)||!tb_next(&q)||!tb_word(&q,"mesh3d")||!tb_done(&q)||!tb_emit(f,s,"descriptor.vol",0,q.p,n))return false;
 while(q.p<n){unsigned kind=0;start=q.p;if(tb_stop(pd))return false;if(!tb_next(&q)){if(q.p<n)return false;break;}
  if(tb_word(&q,"endmesh")){if(!tb_done(&q))return false;while(q.p<n)if(!tb_line(&q)||!tb_done(&q))return false;if(!tb_emit(f,s,"terminator.vol",start,n-start,n))return false;break;}
  if(tb_word(&q,"dimension"))kind=1;else if(tb_word(&q,"geomtype"))kind=2;else if(tb_word(&q,"points"))kind=3;
  else if(tb_word(&q,"surfaceelements"))kind=4;else if(tb_word(&q,"volumeelements"))kind=5;
  else if(tb_word(&q,"edgesegmentsgi2"))kind=6;else if(tb_word(&q,"edgesegments"))kind=7;else if(tb_word(&q,"face_colours"))kind=8;else return false;
  if((seen&(1U<<kind))||!tb_done(&q)||!tb_next(&q)||!tb_i(&q,&count)||!tb_done(&q))return false;seen|=1U<<kind;
  if(kind<=2){if(kind==1){if(count<2||count>3)return false;dimension=count;}else if(count<0||count>2)return false;}
  else{if(!dimension||count<0||count>1000000)return false;if(kind==3)np=(uint32_t)count;if(kind==4)surfaces=(uint32_t)count;if(kind==5)volumes=(uint32_t)count;
   for(i=0;i<count;++i){int32_t v,points=0;double value;if(tb_stop(pd)||!tb_next(&q))return false;
    if(kind==3){if(!tb_nums(&q,3))return false;continue;}
    if(kind==8){if(!tb_i(&q,&v)||v<1)return false;for(j=0;j<3;++j)if(!tb_num(&q,&value)||value<0||value>1)return false;if(!tb_done(&q))return false;continue;}
    if(kind==4){for(j=0;j<4;++j)if(!tb_i(&q,&v)||v<0)return false;if(!tb_i(&q,&points)||(points!=3&&points!=4&&points!=6&&points!=8))return false;}
    else if(kind==5){if(!tb_i(&q,&v)||v<1||!tb_i(&q,&points)||(points!=4&&points!=5&&points!=6&&points!=8&&points!=10&&points!=20))return false;}
    else{int32_t used[2];unsigned fields=kind==6?12:8;for(j=0;j<(int32_t)fields;++j){if(j>=10){if(!tb_num(&q,&value))return false;}else{if(!tb_i(&q,&v))return false;if(j==2||j==3){if(v<1)return false;used[j-2]=v;if((uint32_t)v>maxpoint)maxpoint=(uint32_t)v;}}}if(used[0]==used[1]||!tb_done(&q))return false;continue;}
    {int32_t used[20],k;for(j=0;j<points;++j){if(!tb_i(&q,&v)||v<1)return false;for(k=0;k<j;++k)if(used[k]==v)return false;used[j]=v;if((uint32_t)v>maxpoint)maxpoint=(uint32_t)v;}}
    if(!tb_done(&q))return false;
   }
  }
  xx_rt_snprintf(label,sizeof(label),"section-%u.vol",kind);if(!tb_emit(f,s,label,start,q.p-start,n))return false;
 }
 if(!(seen&2)||np<3||maxpoint>np||(!surfaces&&!volumes))return false;if(!tb_cover(f,s,"comments.vol",n))return false;s->size=(int64_t)n;return true;
}

void xx_netgen_vol_init(xx_netgen_vol *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_NETGEN_VOL,"vol");}}
xx_netgen_vol *xx_netgen_vol_create(xx_io_device *d,int64_t at) {xx_netgen_vol *r=(xx_netgen_vol *)xx_mem_alloc(sizeof(*r));if(r)xx_netgen_vol_init(r,d,at);return r;}
void xx_netgen_vol_destroy(xx_netgen_vol *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_netgen_vol_free(xx_netgen_vol *r) {if(r){xx_netgen_vol_destroy(r);xx_mem_free(r);}}
bool xx_netgen_vol_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_netgen_vol_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
