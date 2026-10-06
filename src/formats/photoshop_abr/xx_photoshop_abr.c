/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Reference: https://raw.githubusercontent.com/GNOME/gimp/master/app/core/gimpbrush-load.c
 * ABR1/2 sampled8-bit brushes only: complete count/record framing, bounded UTF16 names/geometry, stored or fully validated row PackBits. Original brush metadata and encoded bitmap payloads exported; computed brushes and ABR6+ unsupported.
 * Limits64MiB input,4096 components; encoded assets are never executed.
 */
#include "xxfclib/formats/photoshop_abr/xx_photoshop_abr.h"
#include "../wavefront_obj/xx_eleventh_media.h"
static bool eg_quick(Abstractformat *f,uint64_t n) {uint8_t b[4];return n>=42&&pm_read(f,0,b,4)&&(pm_be16(b)==1||pm_be16(b)==2);}
static bool ab_pack(const uint8_t *b,uint64_t at,uint64_t end,uint32_t width) {
 uint32_t out=0;while(at<end){uint8_t c=b[at++];uint32_t run;if(c==128)continue;run=c<128?(uint32_t)(c+1):257U-c;if(run>width-out)return false;if(c<128){if(!eg_span(at,run,end))return false;at+=run;}else if(at<end)++at;else return false;out+=run;}return out==width;
}
static bool eg_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint32_t version=pm_be16(b),count=pm_be16(b+2),i;uint64_t p=4,budget=0;
 if((version!=1&&version!=2)||!count||count>1024||!eg_emit(f,s,"descriptor.abr",0,4,n))return false;
 for(i=0;i<count;++i){uint64_t start=p,end,data;uint32_t size,w,h,j;int64_t top,left,bottom,right;uint8_t compression;char label[64];
  if(eg_stop(pd)||!eg_span(p,6,n)||pm_be16(b+p)!=2) {return false; } size=pm_be32(b+p+2);p+=6;if(!eg_span(p,size,n))return false;end=p+size;
  if(!eg_span(p,6,end)||!pm_be16(b+p+4)||pm_be16(b+p+4)>10000) {return false; } p+=6;
  if(version==2){uint32_t units;if(!eg_span(p,4,end))return false;units=pm_be32(b+p);p+=4;if(units>4096||!eg_span(p,(uint64_t)units*2,end))return false;
   for(j=0;j<units;++j){uint16_t c=pm_be16(b+p+j*2);if(c>=0xd800&&c<=0xdbff){if(++j>=units||(c=pm_be16(b+p+j*2))<0xdc00||c>0xdfff)return false;}else if(c>=0xdc00&&c<=0xdfff)return false;}p+=(uint64_t)units*2;}
  if(!eg_span(p,28,end)||b[p]>1) {return false; } ++p;p+=8;top=(int32_t)pm_be32(b+p);left=(int32_t)pm_be32(b+p+4);bottom=(int32_t)pm_be32(b+p+8);right=(int32_t)pm_be32(b+p+12);p+=16;
  if(bottom<=top||right<=left||bottom-top>16384||right-left>16384||pm_be16(b+p)!=8) {return false; } w=(uint32_t)(right-left);h=(uint32_t)(bottom-top);p+=2;compression=b[p++];data=p;
  if((uint64_t)w*h>16777216||budget>16777216-(uint64_t)w*h) {return false; } budget+=(uint64_t)w*h;
  if(compression==0){if(end-p!=(uint64_t)w*h)return false;p=end;}
  else if(compression==1){uint64_t table=p;if(!eg_span(p,(uint64_t)h*2,end))return false;p+=(uint64_t)h*2;
   for(j=0;j<h;++j){uint32_t z=pm_be16(b+table+j*2);if(eg_stop(pd)||!z||!eg_span(p,z,end)||!ab_pack(b,p,p+z,w))return false;p+=z;}if(p!=end)return false;}
  else return false;
  xx_rt_snprintf(label,sizeof(label),"brush-%u-descriptor.abr",i);if(!eg_emit(f,s,label,start,data-start,n))return false;
  xx_rt_snprintf(label,sizeof(label),"brush-%u-bitmap.abr",i);if(!eg_emit(f,s,label,data,end-data,n))return false;
 }
 if(p!=n) {return false; } s->size=(int64_t)n;return true;
}

void xx_photoshop_abr_init(xx_photoshop_abr *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_PHOTOSHOP_ABR,"abr");}}
xx_photoshop_abr *xx_photoshop_abr_create(xx_io_device *d,int64_t at) {xx_photoshop_abr *r=(xx_photoshop_abr *)xx_mem_alloc(sizeof(*r));if(r)xx_photoshop_abr_init(r,d,at);return r;}
void xx_photoshop_abr_destroy(xx_photoshop_abr *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_photoshop_abr_free(xx_photoshop_abr *r) {if(r){xx_photoshop_abr_destroy(r);xx_mem_free(r);}}
bool xx_photoshop_abr_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_photoshop_abr_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
