/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://geomview.sourceforge.net/docs/html/OFF.html
 * ASCII OFF polygon meshes with complete counts, finite three-coordinate vertices, bounded nondegenerate index lists and optional RGB/RGBA face colors. COFF/NOFF/higher-dimensional and binary dialects unsupported. Original encoded typed sections exported.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/off_mesh/xx_off_mesh.h"
#include "../wavefront_obj/xx_eleventh_media.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[3];return n>=30&&pm_read(f,0,b,3)&&eg_tag(b,"OFF",3);}
static bool off_next(eg_text *q) {while(q->p<q->end){if(!eg_line(q))return false;if(!eg_done(q))return true;}return false;}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 eg_text q={b,0,n,0,0,0};int32_t nv,nf,ne,i;uint64_t vs,fs;
 if(!eg_utf(b,n,true,pd)||!off_next(&q)||!eg_word(&q,"OFF")||!eg_done(&q)||!off_next(&q)||!eg_i(&q,&nv)||!eg_i(&q,&nf)||!eg_i(&q,&ne)||!eg_done(&q)||nv<3||nv>1000000||nf<1||nf>1000000||ne<0)return false;
 vs=q.p;
 for(i=0;i<nv;++i){if(eg_stop(pd)||!off_next(&q)||!eg_nums(&q,3))return false;}fs=q.p;
 for(i=0;i<nf;++i){int32_t count,j,used[256];unsigned colors=0;double v;
  if(eg_stop(pd)||!off_next(&q)||!eg_i(&q,&count)||count<3||count>256)return false;
  for(j=0;j<count;++j){int32_t k;if(!eg_i(&q,&used[j])||used[j]<0||used[j]>=nv)return false;for(k=0;k<j;++k)if(used[k]==used[j])return false;}
  while(!eg_done(&q)){if(++colors>4||!eg_num(&q,&v)||v<0||v>255)return false;}if(colors!=0&&colors!=3&&colors!=4)return false;
 }
 while(q.p<n){if(!eg_line(&q)||!eg_done(&q))return false;}
 if(!eg_emit(f,s,"descriptor.off",0,vs,n)||!eg_emit(f,s,"vertices.off",vs,fs-vs,n)||!eg_emit(f,s,"polygons.off",fs,n-fs,n))return false;s->size=(int64_t)n;return true;
}

void xx_off_mesh_init(xx_off_mesh *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_OFF_MESH,"off");}}
xx_off_mesh *xx_off_mesh_create(xx_io_device *d,int64_t at) {xx_off_mesh *r=(xx_off_mesh *)xx_mem_alloc(sizeof(*r));if(r)xx_off_mesh_init(r,d,at);return r;}
void xx_off_mesh_destroy(xx_off_mesh *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_off_mesh_free(xx_off_mesh *r) {if(r){xx_off_mesh_destroy(r);xx_mem_free(r);}}
bool xx_off_mesh_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_off_mesh_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
