/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/nipy/nibabel/master/nibabel/freesurfer/io.py
 * FreeSurfer binary triangle surfaces: complete creator lines, finite BE32 vertices, bounded nondegenerate triangle indexes and optional typed volume information. Original descriptors/geometry exported; old quad formats and rendering unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/freesurfer_surface/xx_freesurfer_surface.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[3];return n>=12&&pm_read(f,0,b,3)&&b[0]==255&&b[1]==255&&b[2]==254;}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=3,header,vertices,faces,z;uint32_t nv,nf,i,j;unsigned line;
 if(b[0]!=255||b[1]!=255||b[2]!=254)return false;
 for(line=0;line<2;++line){uint64_t start=p;while(p<n&&b[p]!=10){if(p-start>4096) return false;++p;}if(p==n||!tb_utf(b+start,p-start,false,pd))return false;++p;}
 if(!tb_span(p,8,n)||(nv=xx_data_get_u32(b+p, 4, 0, true))<3||nv>1000000||(nf=xx_data_get_u32(b+p+4, 4, 0, true))<1||nf>1000000) {return false; } p+=8;header=p;vertices=p;z=(uint64_t)nv*12;if(!tb_span(p,z,n))return false;
 for(i=0;i<nv;++i){if(tb_stop(pd))return false;for(j=0;j<3;++j)if(!tb_f32(xx_data_get_u32(b+p+(uint64_t)i*12+j*4, 4, 0, true)))return false;}p+=z;faces=p;z=(uint64_t)nf*12;if(!tb_span(p,z,n))return false;
 for(i=0;i<nf;++i){uint32_t a=xx_data_get_u32(b+p+(uint64_t)i*12, 4, 0, true),c=xx_data_get_u32(b+p+(uint64_t)i*12+4, 4, 0, true),d=xx_data_get_u32(b+p+(uint64_t)i*12+8, 4, 0, true);if(tb_stop(pd)||a>=nv||c>=nv||d>=nv||a==c||a==d||c==d)return false;}p+=z;
 if(!tb_emit(f,s,"descriptor.surf",0,header,n)||!tb_emit(f,s,"vertices.surf",vertices,(uint64_t)nv*12,n)||!tb_emit(f,s,"triangles.surf",faces,z,n))return false;
 if(p<n){uint64_t start=p;tb_text q;int32_t v;unsigned k;const char *keys[]={"valid","filename","volume","voxelsize","xras","yras","zras","cras"};
  if(!tb_span(p,4,n)) {return false; } if(xx_data_get_u32(b+p, 4, 0, true)==20)p+=4;else if(tb_span(p,12,n)&&xx_data_get_u32(b+p, 4, 0, true)==2&&xx_data_get_u32(b+p+4, 4, 0, true)==0&&xx_data_get_u32(b+p+8, 4, 0, true)==20)p+=12;else return false;
  xx_mem_zero(&q,sizeof(q));q.b=b;q.p=p;q.end=n;
  for(k=0;k<8;++k){double value;unsigned nums=k==2||k>=3?3:1;if(!tb_line(&q)||!tb_utf(b+q.start,q.stop-q.start,false,pd)||!tb_word(&q,keys[k]))return false;tb_space(&q);if(q.t==q.stop||b[q.t++]!='=')return false;tb_space(&q);
   if(k==1){if(q.t==q.stop||q.stop-q.t>4096)return false;q.t=q.stop;}
   else if(k==0){if(!tb_i(&q,&v)||v<0||v>1)return false;}
   else for(j=0;j<nums;++j){if(k==2){if(!tb_i(&q,&v)||v<1||v>1000000)return false;}else if(!tb_num(&q,&value)||(k==3&&value<=0))return false;}
   if(!tb_done(&q))return false;
  }while(q.p<n)if(!tb_line(&q)||!tb_done(&q))return false;if(!tb_emit(f,s,"volume-info.surf",start,n-start,n))return false;
 }
 s->size=(int64_t)n;return true;
}

void xx_freesurfer_surface_init(xx_freesurfer_surface *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_FREESURFER_SURFACE,"surf");}}
xx_freesurfer_surface *xx_freesurfer_surface_create(xx_io_device *d,int64_t at) {xx_freesurfer_surface *r=(xx_freesurfer_surface *)xx_mem_alloc(sizeof(*r));if(r)xx_freesurfer_surface_init(r,d,at);return r;}
void xx_freesurfer_surface_destroy(xx_freesurfer_surface *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_freesurfer_surface_free(xx_freesurfer_surface *r) {if(r){xx_freesurfer_surface_destroy(r);xx_mem_free(r);}}
bool xx_freesurfer_surface_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_freesurfer_surface_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
