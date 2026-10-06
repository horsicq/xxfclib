/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://docs.safe.com/fme/2025.1/html/FME-Form-Documentation/FME-ReadersWriters/qubiclebinary/quick_facts_qubiclebinary.htm
 * QB1.1 complete positive-sized voxel matrices with UTF8 names, signed positions, RGBA/BGRA and right/left-handed Z flags, exact stored voxel arrays or fully framed slice RLE. Original matrix descriptors and encoded voxel arrays exported; no rendering.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/qubicle_qb/xx_qubicle_qb.h"
#include "../wavefront_obj/xx_eleventh_media.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[24];return n>=54&&pm_read(f,0,b,24)&&pm_le32(b)==0x101U;}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t matrices,i,compression;uint64_t p=24,budget=0;
 if(n<24||pm_le32(b)!=0x101U||pm_le32(b+4)>1||pm_le32(b+8)>1||(compression=pm_le32(b+12))>1||pm_le32(b+16)>1||(matrices=pm_le32(b+20))<1||matrices>1024||!eg_emit(f,s,"descriptor.qb",0,24,n))return false;
 for(i=0;i<matrices;++i){uint64_t start=p,data,voxels;uint32_t x,y,z,k;uint8_t name;char label[64];
  if(eg_stop(pd)||!eg_span(p,1,n)) {return false; } name=b[p++];if(!name||!eg_span(p,(uint64_t)name+24,n)||!eg_utf(b+p,name,false,pd))return false;p+=name;
  x=pm_le32(b+p);y=pm_le32(b+p+4);z=pm_le32(b+p+8);p+=24;
  if(!x||!y||!z||x>4096||y>4096||z>4096) {return false; } voxels=(uint64_t)x*y*z;if(voxels>16777216||budget>16777216-voxels)return false;budget+=voxels;data=p;
  if(!compression){if(!eg_span(p,voxels*4,n))return false;p+=voxels*4;}
  else for(k=0;k<z;++k){uint64_t count=0,plane=(uint64_t)x*y;
   for(;;){uint32_t u;if(eg_stop(pd)||!eg_span(p,4,n))return false;u=pm_le32(b+p);p+=4;
    if(u==6){if(count!=plane) return false;break;}
    if(u==2){uint32_t run;if(!eg_span(p,8,n))return false;run=pm_le32(b+p);p+=8;if(!run||run>plane-count)return false;count+=run;}
    else{if(count==plane)return false;++count;}
   }
  }
  xx_rt_snprintf(label,sizeof(label),"matrix-%u-descriptor.qb",i);if(!eg_emit(f,s,label,start,data-start,n))return false;
  xx_rt_snprintf(label,sizeof(label),"matrix-%u-voxels.qb",i);if(!eg_emit(f,s,label,data,p-data,n))return false;
 }
 if(p!=n) {return false; } s->size=(int64_t)n;return true;
}

void xx_qubicle_qb_init(xx_qubicle_qb *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_QUBICLE_QB,"qb");}}
xx_qubicle_qb *xx_qubicle_qb_create(xx_io_device *d,int64_t at) {xx_qubicle_qb *r=(xx_qubicle_qb *)xx_mem_alloc(sizeof(*r));if(r)xx_qubicle_qb_init(r,d,at);return r;}
void xx_qubicle_qb_destroy(xx_qubicle_qb *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_qubicle_qb_free(xx_qubicle_qb *r) {if(r){xx_qubicle_qb_destroy(r);xx_mem_free(r);}}
bool xx_qubicle_qb_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_qubicle_qb_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
