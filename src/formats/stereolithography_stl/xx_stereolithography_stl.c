/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/mikedh/trimesh/main/trimesh/exchange/stl.py
 * Binary STL with exact84+50*facet count framing,1-1000000 finite nondegenerate triangular facets and zero attribute words. Original84-byte header and facet array exported; ASCII STL, color/attribute dialects and rendering unsupported. Signatureless detection/search is offset-zero only.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#include "xxfclib/formats/stereolithography_stl/xx_stereolithography_stl.h"
#include "../common/xx_audiovisual_components.h"
static bool audiovisual_quick(Abstractformat *f,uint64_t n) { uint8_t h[84];uint32_t count;return audiovisual_probe(f,n,h,84)&&(count=xx_data_get_u32(h+80, 4, 0, false))>0&&count<=1000000&&84+(uint64_t)count*50==n; }
static bool audiovisual_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t count,i,j;uint64_t at;if(n<84||!(count=xx_data_get_u32(b+80, 4, 0, false))||count>1000000||84+(uint64_t)count*50!=n)return false;
 for(i=0,at=84;i<count;++i,at+=50){float xyz[9];double ax,ay,az,bx,by,bz,cx,cy,cz;if(audiovisual_stop(pd)||xx_data_get_u16(b+at+48, 2, 0, false))return false;
  for(j=0;j<12;++j) {if(!audiovisual_finite32(b+at+j*4))return false; } xx_rt_memcpy(xyz,b+at+12,sizeof(xyz));
  for(j=0;j<9;++j)if(xyz[j]<-1e20f||xyz[j]>1e20f)return false;
  ax=(double)xyz[3]-xyz[0];ay=(double)xyz[4]-xyz[1];az=(double)xyz[5]-xyz[2];bx=(double)xyz[6]-xyz[0];by=(double)xyz[7]-xyz[1];bz=(double)xyz[8]-xyz[2];cx=ay*bz-az*by;cy=az*bx-ax*bz;cz=ax*by-ay*bx;
  if(cx==0&&cy==0&&cz==0)return false;
 }
 if(!audiovisual_emit(f,s,"header.bin",0,84,n)||!audiovisual_emit(f,s,"facets.bin",84,n-84,n)) {return false; } s->size=(int64_t)n;return true;
}

void xx_stereolithography_stl_init(xx_stereolithography_stl *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_STEREOLITHOGRAPHY_STL,"stl");}}
xx_stereolithography_stl *xx_stereolithography_stl_create(xx_io_device *d,int64_t at) {xx_stereolithography_stl *r=(xx_stereolithography_stl *)xx_mem_alloc(sizeof(*r));if(r)xx_stereolithography_stl_init(r,d,at);return r;}
void xx_stereolithography_stl_destroy(xx_stereolithography_stl *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_stereolithography_stl_free(xx_stereolithography_stl *r) {if(r){xx_stereolithography_stl_destroy(r);xx_mem_free(r);}}
bool xx_stereolithography_stl_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_stereolithography_stl_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
