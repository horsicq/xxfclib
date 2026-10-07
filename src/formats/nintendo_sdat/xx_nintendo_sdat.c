/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/nintendo_sdat/xx_nintendo_sdat.h"
#include "../nintendo_sdat/xx_twelfth_c.h"
#include "xxfclib/data/xx_data.h"

static bool sd_nitro(tc_blob *b,uint32_t a,uint32_t z) {
 const uint8_t *p=b->p;uint32_t size,at,n,i;if(z<16 || xx_data_get_u16(p+a+4, 2, 0, false)!=0xfeff || xx_data_get_u16(p+a+6, 2, 0, false)!=0x100 || xx_data_get_u16(p+a+12, 2, 0, false)!=16) return false;
 if(xx_rt_memcmp(p+a,"SSEQ",4) && xx_rt_memcmp(p+a,"SSAR",4) && xx_rt_memcmp(p+a,"SBNK",4) && xx_rt_memcmp(p+a,"SWAR",4) && xx_rt_memcmp(p+a,"STRM",4)) return false;
 size=xx_data_get_u32(p+a+8, 4, 0, false);n=xx_data_get_u16(p+a+14, 2, 0, false);if(size<24 || size>z || !n || n>3 || !tc_zero(p+a+size,z-size)) return false;
 at=16;for(i=0;i<n;++i) {uint32_t s;if(!tc_work(b,1) || size-at<8) return false;s=xx_data_get_u32(p+a+at+4, 4, 0, false);
  if(s<8 || s>size-at || (xx_rt_memcmp(p+a+at,"DATA",4) && xx_rt_memcmp(p+a+at,"HEAD",4) && xx_rt_memcmp(p+a+at,"ADPC",4))) { return false; } at+=s;
 }return at==size;
}
static bool read_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 const uint8_t *p=b->p;uint32_t offsets[4],sizes[4],parts[8],counts[8],records[8],a,z,i,j,k,files,info,fat,file,syms,nblock=0,extcount=0;tc_extent ext[4096];char label[64];
 if(b->n<64 || xx_rt_memcmp(p,"SDAT",4) || xx_data_get_u16(p+4, 2, 0, false)!=0xfeff || xx_data_get_u16(p+6, 2, 0, false)!=0x100 || xx_data_get_u32(p+8, 4, 0, false)!=b->n || xx_data_get_u16(p+12, 2, 0, false)!=64 || !tc_zero(p+48,16)) return false;
 for(i=0;i<4;++i) {offsets[i]=xx_data_get_u32(p+16+i*8, 4, 0, false);sizes[i]=xx_data_get_u32(p+20+i*8, 4, 0, false);if(!offsets[i]) {if(i || sizes[i]) return false;continue;}
  if(offsets[i]<64 || sizes[i]<8 || !tc_claim(b,ext,&extcount,offsets[i],sizes[i],false) || xx_data_get_u32(p+offsets[i]+4, 4, 0, false)!=sizes[i]) { return false; } ++nblock;}
 if(xx_data_get_u16(p+14, 2, 0, false)!=nblock || !offsets[1] || !offsets[2] || !offsets[3]) { return false; } info=offsets[1];fat=offsets[2];file=offsets[3];syms=offsets[0];
 if(xx_rt_memcmp(p+info,"INFO",4) || sizes[1]<40 || xx_rt_memcmp(p+fat,"FAT ",4) || sizes[2]<12 || xx_rt_memcmp(p+file,"FILE",4) || sizes[3]<24 || !tc_zero(p+file+12,12)) return false;
 files=xx_data_get_u32(p+fat+8, 4, 0, false);if(!files || files>1024 || sizes[2]!=12+16*files || xx_data_get_u32(p+file+8, 4, 0, false)!=files) return false;
 for(i=0;i<8;++i) {uint32_t rel=xx_data_get_u32(p+info+8+4*i, 4, 0, false);if(rel<40 || rel>sizes[1]-4) return false;parts[i]=info+rel;counts[i]=xx_data_get_u32(p+parts[i], 4, 0, false);if(counts[i]>1024 || counts[i]>(sizes[1]-rel-4)/4) return false;records[i]=parts[i]+4;}
 for(i=0;i<8;++i) for(j=0;j<counts[i];++j) {uint32_t q=xx_data_get_u32(p+records[i]+4*j, 4, 0, false),min[]={10,4,12,4,8,4,17,8};if(!q) continue;
  if(!tc_work(b,1) || q<40 || q>sizes[1] || min[i]>sizes[1]-q) { return false; } q+=info;
  if(i<4 || i==7) {uint32_t id=xx_data_get_u16(p+q, 2, 0, false);static const char *types[]={"SSEQ","SSAR","SBNK","SWAR"};if(id>=files) return false;a=xx_data_get_u32(p+fat+12+16*id, 4, 0, false);z=xx_data_get_u32(p+fat+16+16*id, 4, 0, false);if(!tc_span(b,a,z) || z<16 || xx_rt_memcmp(p+a,i==7?"STRM":types[i],4)) return false;}
  if(i==0) {if(xx_data_get_u16(p+q+4, 2, 0, false)>=counts[2] || p[q+6]>127 || p[q+9]>=counts[4]) return false;}
  if(i==2) for(k=0;k<4;++k) {int32_t id=(int16_t)xx_data_get_u16(p+q+4+k*2, 2, 0, false);if(id!=-1 && (id<0 || (uint32_t)id>=counts[3])) return false;}
  if(i==5) {uint32_t n=xx_data_get_u32(p+q, 4, 0, false);if(n>1024 || n>(info+sizes[1]-q-4)/8) return false;for(k=0;k<n;++k) {uint32_t v=q+4+k*8,t=p[v],id=xx_data_get_u32(p+v+4, 4, 0, false);if(!tc_work(b,1) || t>3 || p[v+3] || id>=counts[t] || (xx_data_get_u16(p+v+1, 2, 0, false)&~7U)) return false;}}
  if(i==6) {uint32_t n=p[q],mask=0;if(!n || n>16) return false;for(k=0;k<n;++k) {uint32_t c=p[q+1+k];if(c>15 || (mask&(1U<<c))) return false;mask|=1U<<c;}}
  if(i==7 && (p[q+4]>127 || p[q+6]>=counts[6])) return false;
 }
 if(syms) {if(sizes[0]<40 || xx_rt_memcmp(p+syms,"SYMB",4)) return false;
  for(i=0;i<8;++i) {uint32_t rel=xx_data_get_u32(p+syms+8+i*4, 4, 0, false),n;if(!rel) {if(counts[i]) return false;continue;}if(rel<40 || rel>sizes[0]-4) return false;a=syms+rel;n=xx_data_get_u32(p+a, 4, 0, false);if(n!=counts[i] || n>(sizes[0]-rel-4)/(i==1?8:4)) return false;
   for(j=0;j<n;++j) {uint32_t ptr=xx_data_get_u32(p+a+4+j*(i==1?8:4), 4, 0, false);if(ptr) {if(ptr<40 || ptr>=sizes[0] || !tc_string(b,syms+ptr,syms+sizes[0],&z,false)) return false;}
    if(i==1) {uint32_t sub=xx_data_get_u32(p+a+8+j*8, 4, 0, false),cnt;if(!sub) continue;if(sub<40 || sub>sizes[0]-4) return false;cnt=xx_data_get_u32(p+syms+sub, 4, 0, false);if(cnt>1024 || cnt>(sizes[0]-sub-4)/4) return false;
     for(k=0;k<cnt;++k) {ptr=xx_data_get_u32(p+syms+sub+4+k*4, 4, 0, false);if(ptr && (ptr<40 || ptr>=sizes[0] || !tc_string(b,syms+ptr,syms+sizes[0],&z,false))) return false;}}
   }
  }if(!tc_emit(f,s,b,"symbol-index.bin",syms,sizes[0])) return false;
 }
 if(!tc_emit(f,s,b,"sound-info.bin",info,sizes[1]) || !tc_emit(f,s,b,"file-allocation-table.bin",fat,sizes[2])) return false;
 extcount=0;for(i=0;i<files;++i) {uint32_t q=fat+12+i*16;a=xx_data_get_u32(p+q, 4, 0, false);z=xx_data_get_u32(p+q+4, 4, 0, false);
  if(a<file+24 || a>file+sizes[3] || z>file+sizes[3]-a || !tc_zero(p+q+8,8) || !tc_claim(b,ext,&extcount,a,z,false) || !sd_nitro(b,a,z)) return false;
  xx_rt_snprintf(label,sizeof(label),"sound-file-%04u.%c%c%c%c",i,p[a],p[a+1],p[a+2],p[a+3]);if(!tc_emit(f,s,b,label,a,z)) return false;
 }if(file+sizes[3]!=b->n) return false;s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { tc_blob b;bool ok;if(!tc_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_nintendo_sdat_init(xx_nintendo_sdat *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_SDAT,"nintendo_sdat");} }
xx_nintendo_sdat *xx_nintendo_sdat_create(xx_io_device *d,int64_t b) { xx_nintendo_sdat *r=(xx_nintendo_sdat *)xx_mem_alloc(sizeof(*r));if(r) xx_nintendo_sdat_init(r,d,b);return r; }
void xx_nintendo_sdat_destroy(xx_nintendo_sdat *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_sdat_free(xx_nintendo_sdat *r) { if(r) {xx_nintendo_sdat_destroy(r);xx_mem_free(r);} }
bool xx_nintendo_sdat_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_sdat_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
