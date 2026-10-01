/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/assimp/assimp/master/code/AssetLib/LWO/LWOFileData.h
 * LWO2 single-layer polygon meshes with LAYR,PNTS,POLS FACE,TAGS and optional PTAG SURF. Checks IFF padding, unique section framing, finite geometry, VX point/polygon references and tag indices. Up to65536 points/polygons/tags. Exports each encoded chunk; surfaces, vertex maps, envelopes, other polygon types and rendering unsupported.
 */
#include "xxfclib/formats/lightwave_lwo2/xx_lightwave_lwo2.h"
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

static bool vx(const uint8_t *b,uint64_t *p,uint64_t end,uint32_t *value) {if(!span(*p,2,end))return false;if(b[*p]==255){if(!span(*p,4,end))return false;*value=be24(b+*p+1);*p+=4;}else{*value=pm_be16(b+*p);*p+=2;}return true;}

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 uint64_t end,at=12,polys=0,polys_end=0,ptag=0,ptag_end=0;uint32_t nv=0,np=0,nt=0;bool layer=false,tags=false;char label[40];
 if(n<12||xx_rt_memcmp(b,"FORM",4)||xx_rt_memcmp(b+8,"LWO2",4)||(end=8+(uint64_t)pm_be32(b+4))<12||end>n)return false;
 while(at<end){uint32_t len,id;uint64_t p,e;if(stop(pd)||!span(at,8,end)||!span(at+8,len=pm_be32(b+at+4),end))return false;id=pm_be32(b+at);p=at+8;e=p+len;
 switch(id){case 0x4c415952U:if(layer||len<18||pm_be16(b+p)||pm_be16(b+p+2)||!floats(b,p+4,3,true,e))return false;p+=16;if(!cstr(b,&p,e,1024,true))return false;if(p&1)++p;if(p!=e)return false;layer=true;break;
 case 0x504e5453U:if(nv||!len||len%12||(nv=len/12)>65536||!floats(b,p,(uint64_t)nv*3,true,e))return false;break;
 case 0x504f4c53U:if(polys||len<4||xx_rt_memcmp(b+p,"FACE",4))return false;polys=p+4;polys_end=e;break;
 case 0x54414753U:if(tags)return false;tags=true;while(p<e){if(++nt>65536||!cstr(b,&p,e,1024,false))return false;if(p&1)++p;}if(p!=e)return false;break;
 case 0x50544147U:if(ptag||len<4||xx_rt_memcmp(b+p,"SURF",4))return false;ptag=p+4;ptag_end=e;break;default:return false;}
 xx_rt_snprintf(label,sizeof(label),"chunk-%u.bin",(unsigned)s->count);if(!emit(f,s,label,at,8+len,end))return false;at=e+(len&1);if(at>end||(len&1&&b[e]))return false; }
 if(!layer||!nv||!polys||!tags||!nt)return false;at=polys;while(at<polys_end){uint32_t count,j,index;if(!span(at,2,polys_end)||++np>65536)return false;count=pm_be16(b+at);at+=2;if(count<3||count>1024)return false;for(j=0;j<count;++j)if(!vx(b,&at,polys_end,&index)||index>=nv)return false;}
 at=ptag;while(ptag&&at<ptag_end){uint32_t index;if(!vx(b,&at,ptag_end,&index)||index>=np||!span(at,2,ptag_end)||pm_be16(b+at)>=nt)return false;at+=2;}if(!np)return false;s->size=(int64_t)end;return true;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<4 || !pm_read(f,0,probe,4) || xx_rt_memcmp(probe,"\x46\x4f\x52\x4d",4)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_lightwave_lwo2_init(xx_lightwave_lwo2 *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_LIGHTWAVE_LWO2,"lwo");} }
xx_lightwave_lwo2 *xx_lightwave_lwo2_create(xx_io_device *d,int64_t b) {xx_lightwave_lwo2 *r=(xx_lightwave_lwo2 *)xx_mem_alloc(sizeof(*r));if(r)xx_lightwave_lwo2_init(r,d,b);return r;}
void xx_lightwave_lwo2_destroy(xx_lightwave_lwo2 *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_lightwave_lwo2_free(xx_lightwave_lwo2 *r) {if(r){xx_lightwave_lwo2_destroy(r);xx_mem_free(r);}}
bool xx_lightwave_lwo2_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_lightwave_lwo2_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
