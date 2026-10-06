/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/blender/blender-addons/main/io_scene_fbx/encode_bin.py
 * Binary FBX7400/7500, bounded recursive node/property grammar with scalar/string/raw and stored/zlib array properties. Validates finite floats, exact array inflation size and Adler32, child sentinels and standard footer/version. Depth32,65536 nodes,4096 root members,16MiB per array and64MiB cumulative expanded arrays. Exports each original root subtree plus footer; semantic object connections, rendering and ASCII FBX unsupported.
 */
#include "xxfclib/formats/autodesk_fbx/xx_autodesk_fbx.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"

static __inline bool span(uint64_t a,uint64_t n,uint64_t e) { return a<=e && n<=e-a; }
static __inline bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static __inline uint64_t u64(const uint8_t *p,bool be) { return be ? ((uint64_t)pm_be32(p)<<32)|pm_be32(p+4) : ((uint64_t)pm_le32(p+4)<<32)|pm_le32(p); }
static __inline uint32_t u32(const uint8_t *p,bool be) { return be ? pm_be32(p):pm_le32(p); }
static __inline uint16_t u16(const uint8_t *p,bool be) { return be ? pm_be16(p):pm_le16(p); }
static __inline uint32_t be24(const uint8_t *p) { return (uint32_t)p[0]<<16 | (uint32_t)p[1]<<8 | p[2]; }
static __inline bool zero(const uint8_t *b,uint64_t n) { uint64_t i; for(i=0;i<n;++i) if(b[i]) return false; return true; }
static __inline bool finite32(const uint8_t *p,bool be) { return (u32(p,be)&0x7f800000U)!=0x7f800000U; }
static __inline bool finite64(const uint8_t *p,bool be) { return (u64(p,be)&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL; }
static __inline bool floats(const uint8_t *b,uint64_t at,uint64_t count,bool be,uint64_t n) { uint64_t i; if(!span(at,count*4,n)) return false; for(i=0;i<count;++i) if(!finite32(b+at+i*4,be)) return false; return true; }
static __inline bool emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t a,uint64_t n,uint64_t e) { return span(a,n,e) && s->count<4096 && pm_add(f,s,label,(int64_t)a,(int64_t)n); }
static __inline bool cstr(const uint8_t *b,uint64_t *at,uint64_t end,uint64_t maximum,bool empty) { uint64_t start=*at; while(*at<end && *at-start<=maximum) { uint8_t c=b[(*at)++]; if(!c) return empty || *at>start+1; if(c<32 || c==127) return false; } return false; }
typedef struct range { uint64_t at,n; } range;
static __inline bool reserve(range *r,unsigned *nr,unsigned max,uint64_t at,uint64_t n,uint64_t lo,uint64_t end) { unsigned i; if(*nr>=max || at<lo || !span(at,n,end)) return false; for(i=0;i<*nr;++i) if(n && r[i].n && at<r[i].at+r[i].n && r[i].at<at+n) return false; r[*nr].at=at;r[*nr].n=n;++*nr;return true; }
static __inline uint32_t crc32_bytes(const uint8_t *b,uint64_t n) { return xx_crc32_calc(0U, b, (size_t)n); }

#include "xxfclib/algo/deflate/xx_deflate.h"
static bool fbx_properties(const uint8_t *b,uint64_t *p,uint64_t end,uint64_t count,uint64_t *budget,xx_pd_struct *pd) {
 uint64_t i; if(count>65536)return false;for(i=0;i<count;++i) {uint8_t t;uint64_t width=0;if(stop(pd)||*p>=end)return false;t=b[(*p)++];switch(t) {case 'Y':width=2;break;case 'C':width=1;break;case 'I':case 'F':width=4;break;case 'L':case 'D':width=8;break;
 case 'S':case 'R':if(!span(*p,4,end))return false;width=u32(b+*p,false);*p+=4;break;
 case 'f':case 'd':case 'l':case 'i':case 'b':case 'c': {uint32_t c,encoding,packed;uint64_t raw;uint8_t *owned=NULL;const uint8_t *data;size_t written=0;uint32_t j;bool okay=true;
   if(!span(*p,12,end)) {return false; } c=u32(b+*p,false);encoding=u32(b+*p+4,false);packed=u32(b+*p+8,false);*p+=12;width=(t=='d'||t=='l')?8:(t=='f'||t=='i')?4:1;raw=(uint64_t)c*width;
   if(encoding>1||raw>16777216||raw>*budget||!span(*p,packed,end)) {return false; } *budget-=raw;data=b+*p;
   if(encoding) {owned=(uint8_t *)xx_mem_alloc((size_t)(raw?raw:1));if(!owned)return false;okay=xx_zlib_stream_decode_memory(data,packed,owned,(size_t)raw,&written)&&written==raw&&xx_zlib_stream_trailer_matches(data,packed,owned,(size_t)raw);data=owned;}else if(packed!=raw)okay=false;
   if(okay)for(j=0;j<c;++j) {if((t=='f'&&!finite32(data+(uint64_t)j*4,false))||(t=='d'&&!finite64(data+(uint64_t)j*8,false))||(t=='b'&&data[j]>1)){okay=false;break;}}
   if(owned) {xx_mem_free(owned); } if(!okay)return false;*p+=packed;continue; }
 default:return false; }
 if(!span(*p,width,end)) {return false; } if((t=='C'&&b[*p]>1)||(t=='F'&&!finite32(b+*p,false))||(t=='D'&&!finite64(b+*p,false)))return false;*p+=width; }return *p==end;
}
static bool fbx_node(const uint8_t *b,uint64_t *at,uint64_t end,unsigned hs,unsigned depth,unsigned *nodes,uint64_t *budget,xx_pd_struct *pd) {
 uint64_t start=*at,finish,count,len,p,prop_end;unsigned name;if(depth>32||++*nodes>65536||stop(pd)||!span(start,hs,end))return false;
 finish=hs==25?u64(b+start,false):u32(b+start,false);count=hs==25?u64(b+start+8,false):u32(b+start+4,false);len=hs==25?u64(b+start+16,false):u32(b+start+8,false);name=b[start+hs-1];p=start+hs;
 if(!name||finish<p||finish>end||!span(p,name,finish)) {return false; } p+=name;if(!span(p,len,finish))return false;prop_end=p+len;if(!fbx_properties(b,&p,prop_end,count,budget,pd))return false;
 if(p<finish) {if(finish-p<hs)return false;while(p<finish-hs)if(!fbx_node(b,&p,finish-hs,hs,depth+1,nodes,budget,pd))return false;if(p!=finish-hs||!zero(b+p,hs))return false;}
 *at=finish;return true;
}

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 uint32_t version;unsigned hs,nodes=0;uint64_t at=27,budget=67108864,footer,end;char label[40];static const uint8_t tail[]={0xf8,0x5a,0x8c,0x6a,0xde,0xf5,0xd9,0x7e,0xec,0xe9,0x0c,0xe3,0x75,0x8f,0x29,0x0b};
 if(n<27||xx_rt_memcmp(b,"Kaydara FBX Binary  \0\x1a\0",23)||((version=u32(b+23,false))!=7400&&version!=7500)) {return false; } hs=version==7500?25:13;
 while(span(at,hs,n)&&!zero(b+at,hs)) {uint64_t start=at;if(!fbx_node(b,&at,n,hs,0,&nodes,&budget,pd))return false;xx_rt_snprintf(label,sizeof(label),"root-%u.fbxnode",(unsigned)s->count);if(!emit(f,s,label,start,at-start,n))return false;}
 if(!s->count||!span(at,hs+20,n)||!zero(b+at,hs)||!zero(b+at+hs+16,4)) {return false; } footer=at+hs;end=footer+20;end=((end+15)&~15ULL)==end?end+16:(end+15)&~15ULL;
 if(!span(end,140,n)||!zero(b+footer+20,end-footer-20)||u32(b+end,false)!=version||!zero(b+end+4,120)||xx_rt_memcmp(b+end+124,tail,16)) {return false; } end+=140;
 if(!emit(f,s,"footer.bin",footer,end-footer,n)) {return false; } s->size=(int64_t)end;return true;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<23 || !pm_read(f,0,probe,23) || xx_rt_memcmp(probe,"\x4b\x61\x79\x64\x61\x72\x61\x20\x46\x42\x58\x20\x42\x69\x6e\x61\x72\x79\x20\x20\x00\x1a\x00",23)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_autodesk_fbx_init(xx_autodesk_fbx *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_AUTODESK_FBX,"fbx");} }
xx_autodesk_fbx *xx_autodesk_fbx_create(xx_io_device *d,int64_t b) {xx_autodesk_fbx *r=(xx_autodesk_fbx *)xx_mem_alloc(sizeof(*r));if(r)xx_autodesk_fbx_init(r,d,b);return r;}
void xx_autodesk_fbx_destroy(xx_autodesk_fbx *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_autodesk_fbx_free(xx_autodesk_fbx *r) {if(r){xx_autodesk_fbx_destroy(r);xx_mem_free(r);}}
bool xx_autodesk_fbx_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_autodesk_fbx_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
