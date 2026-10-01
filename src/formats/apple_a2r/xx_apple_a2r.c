/* SPDX-License-Identifier: MIT. Independently implemented primary framing.
 * Inert encoded components only: no playback, emulation or filesystem recovery.
 */
#include "xxfclib/formats/apple_a2r/xx_apple_a2r.h"
#include "../nintendo_sdat/xx_twelfth_c.h"

static bool a2_meta(tc_blob *b,uint32_t a,uint32_t z) {
 uint32_t end=a+z,starts[256],lens[256],count=0;if(!z || b->p[end-1]!=10) return false;
 while(a<end) {uint32_t start=a,key,i;while(a<end && b->p[a]!=9) {if(b->p[a]<=32 || b->p[a]>=127 || !tc_work(b,1)) return false;++a;}
  key=a-start;if(!key || key>255 || a==end || count==256) return false;
  for(i=0;i<count;++i) {if(!tc_work(b,key+1)) return false;if(lens[i]==key && !xx_rt_memcmp(b->p+start,b->p+starts[i],key)) return false;}starts[count]=start;lens[count++]=key;++a;start=a;
  while(a<end && b->p[a]!=10) {if(b->p[a]<32 || !tc_work(b,1)) return false;++a;}if(a==end || !tc_utf8(b,start,a-start)) return false;++a;
 }return true;
}
/* A2R 3.x uses RWCP rather than the 2.x STRM layout. Keep each capture's
 * descriptor (including the absolute index times) beside its raw bit/flux
 * bytes: neither member needs a decoder or a guessed disk geometry. */
static bool read_v3_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 uint32_t a=8,info=0,rwcp=0,meta=0,captures=0,unknown=0;
 const uint8_t *p=b->p;char label[96];uint8_t drive=0;
 if(b->n<53 || xx_rt_memcmp(p,"A2R3\xff\x0a\x0d\x0a",8)) return false;
 while(a<b->n) {
  uint32_t z,start,end;
  if(!tc_span(b,a,8) || !tc_work(b,1)) return false;
  z=pm_le32(p+a+4);start=a+8;
  if(!tc_span(b,start,z)) return false;
  end=start+z;
  if(!xx_rt_memcmp(p+a,"INFO",4)) {
   if(info++ || a!=8 || z!=37 || p[start]!=1 ||
      !tc_utf8(b,start+1,32) ||
      !(drive=p[start+33]) || drive>8 ||
      p[start+34]>1 || p[start+35]>1 ||
      !tc_emit(f,s,b,"capture-info.bin",start,z)) return false;
  } else if(!xx_rt_memcmp(p+a,"RWCP",4)) {
   uint32_t at,chunk=rwcp++;
   if(!info || z<17 || p[start]!=1 || !pm_le32(p+start+1) ||
      !tc_zero(p+start+5,11)) return false;
   xx_rt_snprintf(label,sizeof(label),"rwcp-%04u-header.bin",chunk);
   if(!tc_emit(f,s,b,label,start,16)) return false;
   at=start+16;
   while(at<end && p[at]=='C') {
    uint32_t idx_bytes,size_at,size,entry,loc,i,previous=0;
    uint64_t ticks=0;uint8_t type,indexes;
    if(end-at<9 || !tc_work(b,1)) return false;
    type=p[at+1];loc=pm_le16(p+at+2);indexes=p[at+4];
    idx_bytes=(uint32_t)indexes*4U;
    if(type<1 || type>3 || loc>159 ||
       ((drive==4 || drive==8) && loc>79) ||
       end-at<9U+idx_bytes) return false;
    size_at=at+5U+idx_bytes;size=pm_le32(p+size_at);
    entry=size_at+4U;
    if(!size || size>end-entry || !tc_work(b,size)) return false;
    if(type==2) {if(size!=16384U) return false;}
    else {
     for(i=0;i<size;++i) {
      if(!(i&4095U) && !tc_poll(b)) return false;
      ticks+=p[entry+i];
     }
     if(!ticks || p[entry+size-1U]==255U) return false;
    }
    for(i=0;i<indexes;++i) {
     uint32_t current=pm_le32(p+at+5U+i*4U);
     if(!current || current<=previous) return false;
     previous=current;
    }
    xx_rt_snprintf(label,sizeof(label),"capture-%04u-%03u.descriptor.bin",captures,loc);
    if(!tc_emit(f,s,b,label,at,9U+idx_bytes)) return false;
    xx_rt_snprintf(label,sizeof(label),"capture-%04u-%03u.%s",captures,loc,type==2?"bits":"flux");
    if(!tc_emit(f,s,b,label,entry,size)) return false;
    ++captures;at=entry+size;
   }
   if(at!=end-1U || p[at]!='X') return false;
  } else if(!xx_rt_memcmp(p+a,"META",4)) {
   if(!info || meta++ || !a2_meta(b,start,z) ||
      !tc_emit(f,s,b,"capture-metadata.txt",start,z)) return false;
  } else if(!xx_rt_memcmp(p+a,"SLVD",4)) {
   /* Solved tracks have a different record layout; refusing the image avoids
    * silently dropping them when both RWCP and SLVD are present. */
   return false;
  } else {
   uint32_t i;
   for(i=0;i<4;++i) if(p[a+i]<33 || p[a+i]>126) return false;
   if(!info || unknown==4096) return false;
   if(z) {
    xx_rt_snprintf(label,sizeof(label),"unknown-%04u-%c%c%c%c.bin",unknown,
                   p[a],p[a+1],p[a+2],p[a+3]);
    if(!tc_emit(f,s,b,label,start,z)) return false;
   }
   ++unknown;
  }
  a=end;
 }
 if(!info || !rwcp || !captures) return false;
 s->size=b->n;return true;
}
static bool read_components(Abstractformat *f,pm_stream *s,tc_blob *b) {
 uint32_t a=8,z,start,count=0,info=0,strm=0,meta=0;const uint8_t *p=b->p;char label[64];
 if(b->n>=8 && !xx_rt_memcmp(p,"A2R3\xff\x0a\x0d\x0a",8))
  return read_v3_components(f,s,b);
 if(b->n<52 || xx_rt_memcmp(p,"A2R2\xff\x0a\x0d\x0a",8)) return false;
 while(a<b->n) {if(!tc_span(b,a,8) || !tc_work(b,1)) return false;z=pm_le32(p+a+4);start=a+8;if(!tc_span(b,start,z)) return false;
  if(!xx_rt_memcmp(p+a,"INFO",4)) {if(info++ || a!=8 || z!=36 || p[start]!=1 || (p[start+33]!=1 && p[start+33]!=2) || p[start+34]>1 || p[start+35]>1 || !tc_utf8(b,start+1,32)) return false;if(!tc_emit(f,s,b,"capture-info.bin",start,z)) return false;}
  else if(!xx_rt_memcmp(p+a,"STRM",4)) {uint32_t at=start,end=start+z;if(!info || strm++ || !z) return false;
   while(at<end && p[at]!=255) {uint32_t n,loop,i;uint64_t ticks=0;uint8_t type;if(!tc_span(b,at,10) || end-at<10 || !tc_work(b,1)) return false;
    type=p[at+1];n=pm_le32(p+at+2);loop=pm_le32(p+at+6);if(p[at]>159 || type<1 || type>3 || !n || n>end-at-10 || !loop) return false;
    if(type==2) {if(n!=16384 || loop>n*8*32) return false;}else {for(i=0;i<n;++i) {if(!(i&4095) && !tc_poll(b)) return false;ticks+=p[at+10+i];}if(p[at+9+n]==255 || !ticks || loop>ticks) return false;}
    xx_rt_snprintf(label,sizeof(label),"capture-%04u-%03u.%s",count,p[at],type==2?"bits":"flux");if(!tc_emit(f,s,b,label,at+10,n)) return false;++count;at+=10+n;
   }if(at!=end-1 || p[at]!=255) return false;
  }else if(!xx_rt_memcmp(p+a,"META",4)) {if(!strm || meta++ || !a2_meta(b,start,z) || !tc_emit(f,s,b,"capture-metadata.txt",start,z)) return false;}
  else return false;a=start+z;
 }if(!info || !strm || !count) return false;s->size=b->n;return true;
}
static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) { tc_blob b;bool ok;if(!tc_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok; }
void xx_apple_a2r_init(xx_apple_a2r *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_APPLE_A2R,"apple_a2r");} }
xx_apple_a2r *xx_apple_a2r_create(xx_io_device *d,int64_t b) { xx_apple_a2r *r=(xx_apple_a2r *)xx_mem_alloc(sizeof(*r));if(r) xx_apple_a2r_init(r,d,b);return r; }
void xx_apple_a2r_destroy(xx_apple_a2r *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_apple_a2r_free(xx_apple_a2r *r) { if(r) {xx_apple_a2r_destroy(r);xx_mem_free(r);} }
bool xx_apple_a2r_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_apple_a2r_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
