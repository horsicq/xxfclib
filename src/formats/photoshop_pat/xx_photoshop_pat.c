/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/SethRobinson/Patchy/main/src/psd/pat_reader.cpp
 * Standalone Photoshop 8BPT PAT1 RGB/grayscale sampled patterns: complete count and UTF16BE names/IDs, VMA3 slots and exact stored/row-PackBits8-bit plane extents. Original encoded pattern records exported; optional typed empty8BIMphry hierarchy accepted. Nonempty hierarchies, other modes/depths/compression and pixel rendering declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#include "xxfclib/formats/photoshop_pat/xx_photoshop_pat.h"
#include "../gimp_gpl/xx_twelfth_b.h"
static bool tb_quick(Abstractformat *f,uint64_t n) {uint8_t b[10];return n>=10&&pm_read(f,0,b,10)&&tb_tag(b,"8BPT",4)&&pm_be16(b+4)==1;}
static bool pa_pack(const uint8_t *b,uint64_t z,uint32_t width,xx_pd_struct *pd) {uint64_t p=0,out=0;while(p<z){int c=(int8_t)b[p++];uint64_t run;if(tb_stop(pd))return false;if(c==-128)continue;run=c<0?(uint64_t)(1-c):(uint64_t)(c+1);if(run>width-out)return false;if(c<0){if(p==z)return false;++p;}else{if(!tb_span(p,run,z))return false;p+=run;}out+=run;}return out==width;}
static bool pa_unicode(const uint8_t *b,uint32_t z) {uint32_t i;for(i=0;i<z;++i){uint16_t c=pm_be16(b+i*2);if(!c){if(i+1!=z)return false;}else if(c>=0xd800&&c<=0xdbff){if(++i==z||(c=pm_be16(b+i*2))<0xdc00||c>0xdfff)return false;}else if(c>=0xdc00&&c<=0xdfff)return false;}return true;}
static bool tb_parse(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {
 uint64_t p=10,start,vm,end,total=0;uint32_t count,i,j,k;count=pm_be32(b+6);if(!count||count>4094||!tb_emit(f,s,"descriptor.pat",0,10,n))return false;
 for(i=0;i<count;++i){uint32_t mode,chars,channels;uint16_t h,w;unsigned colors,written=0;uint8_t id;start=p;
  if(tb_stop(pd)||!tb_span(p,16,n)||pm_be32(b+p)!=1) {return false; } mode=pm_be32(b+p+4);h=pm_be16(b+p+8);w=pm_be16(b+p+10);chars=pm_be32(b+p+12);p+=16;
  if((mode!=1&&mode!=3)||!h||!w||w>16384||h>16384||chars>4096||(uint64_t)w*h>8388608||total+(uint64_t)w*h>16777216||!tb_span(p,(uint64_t)chars*2+1,n)||!pa_unicode(b+p,chars)) {return false; } total+=(uint64_t)w*h;p+=(uint64_t)chars*2;id=b[p++];if(!id||!tb_span(p,id+8U,n)||!tb_utf(b+p,id-(b[p+id-1]==0),false,pd))return false;p+=id;
  vm=p;if(pm_be32(b+p)!=3||!tb_span(p+8,pm_be32(b+p+4),n)||pm_be32(b+p+4)<20)return false;end=p+8+pm_be32(b+p+4);p+=8;
  if(pm_be32(b+p)||pm_be32(b+p+4)||pm_be32(b+p+8)!=h||pm_be32(b+p+12)!=w) {return false; } channels=pm_be32(b+p+16);p+=20;if(channels>64)return false;colors=mode==3?3:1;
  for(j=0;j<channels+2;++j){uint32_t present,z;uint64_t finish;if(!tb_span(p,4,end))return false;present=pm_be32(b+p);p+=4;if(present>1)return false;if(!present){if(j<colors)return false;continue;}
   if(!tb_span(p,4,end)) {return false; } z=pm_be32(b+p);p+=4;finish=p+z;if(z<23||!tb_span(p,z,end)||pm_be32(b+p)!=8||pm_be32(b+p+4)||pm_be32(b+p+8)||pm_be32(b+p+12)!=h||pm_be32(b+p+16)!=w||pm_be16(b+p+20)!=8||b[p+22]>1)return false;
   if(b[p+22]==0){if(z-23!=(uint64_t)w*h)return false;p=finish;}
   else{uint64_t rows=p+23,data=rows+(uint64_t)h*2;if(!tb_span(rows,(uint64_t)h*2,finish))return false;for(k=0;k<h;++k){uint32_t row=pm_be16(b+rows+k*2);if(!tb_span(data,row,finish)||!pa_pack(b+data,row,w,pd))return false;data+=row;}if(data!=finish)return false;p=finish;}++written;
  }if(p!=end||written<colors||!tb_emit(f,s,"pattern.pat",start,p-start,n))return false;(void)vm;
 }if(p<n){const uint8_t *a;uint32_t size;start=p;if(!tb_span(p,12,n)||!tb_tag(b+p,"8BIMphry",8))return false;size=pm_be32(b+p+8);p+=12;if(size!=43||!tb_span(p,size,n))return false;a=b+p;if(pm_be32(a)!=16||pm_be32(a+4)!=1||pm_be16(a+8)||pm_be32(a+10)||!tb_tag(a+14,"null",4)||pm_be32(a+18)!=1||pm_be32(a+22)!=9||!tb_tag(a+26,"hierarchyVlLs",13)||pm_be32(a+39))return false;p+=size;if(!tb_emit(f,s,"empty-hierarchy.pat",start,p-start,n))return false;}if(p!=n)return false;s->size=(int64_t)n;return true;
}

void xx_photoshop_pat_init(xx_photoshop_pat *r,xx_io_device *d,int64_t at) {if(r){xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,at,XX_FILE_TYPE_PHOTOSHOP_PAT,"pat");}}
xx_photoshop_pat *xx_photoshop_pat_create(xx_io_device *d,int64_t at) {xx_photoshop_pat *r=(xx_photoshop_pat *)xx_mem_alloc(sizeof(*r));if(r)xx_photoshop_pat_init(r,d,at);return r;}
void xx_photoshop_pat_destroy(xx_photoshop_pat *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_photoshop_pat_free(xx_photoshop_pat *r) {if(r){xx_photoshop_pat_destroy(r);xx_mem_free(r);}}
bool xx_photoshop_pat_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_photoshop_pat_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
