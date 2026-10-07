/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FNA-XNA/FNA/master/src/Content/ContentReader.cs
 * Uncompressed XNB5, one version0 reader and no shared resources: Texture2DReader(Color/Bgr565/Bgra5551/Bgra4444), StringReader, ByteReader, Int32Reader or SingleReader. Parses canonical7-bit lengths, exact root reader and mip dimensions/byte sizes. Up to16 mips,8192 dimensions,16million pixels,64MiB file. Exports encoded mip/value payloads; LZX/LZ4, complex/custom/shared objects, texture conversion and rendering unsupported.
 */
#include "xxfclib/formats/xna_xnb/xx_xna_xnb.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static __inline bool span(uint64_t a,uint64_t n,uint64_t e) { return a<=e && n<=e-a; }
static __inline bool stop(xx_pd_struct *pd) { return pd && xx_pd_is_stopped(pd); }
static __inline bool zero(const uint8_t *b,uint64_t n) { uint64_t i; for(i=0;i<n;++i) if(b[i]) return false; return true; }
static __inline bool finite32(const uint8_t *p,bool be) { return (xx_data_get_u32(p, 4, 0, be)&0x7f800000U)!=0x7f800000U; }
static __inline bool finite64(const uint8_t *p,bool be) { return (xx_data_get_u64(p, 8, 0, be)&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL; }
static __inline bool floats(const uint8_t *b,uint64_t at,uint64_t count,bool be,uint64_t n) { uint64_t i; if(!span(at,count*4,n)) return false; for(i=0;i<count;++i) if(!finite32(b+at+i*4,be)) return false; return true; }
static __inline bool emit(Abstractformat *f,pm_stream *s,const char *label,uint64_t a,uint64_t n,uint64_t e) { return span(a,n,e) && s->count<4096 && pm_add(f,s,label,(int64_t)a,(int64_t)n); }
static __inline bool cstr(const uint8_t *b,uint64_t *at,uint64_t end,uint64_t maximum,bool empty) { uint64_t start=*at; while(*at<end && *at-start<=maximum) { uint8_t c=b[(*at)++]; if(!c) return empty || *at>start+1; if(c<32 || c==127) return false; } return false; }
typedef struct range { uint64_t at,n; } range;
static __inline bool reserve(range *r,unsigned *nr,unsigned max,uint64_t at,uint64_t n,uint64_t lo,uint64_t end) { unsigned i; if(*nr>=max || at<lo || !span(at,n,end)) return false; for(i=0;i<*nr;++i) if(n && r[i].n && at<r[i].at+r[i].n && r[i].at<at+n) return false; r[*nr].at=at;r[*nr].n=n;++*nr;return true; }

static bool var7(const uint8_t *b,uint64_t *p,uint64_t end,uint32_t *v){unsigned i;uint32_t value=0;for(i=0;i<5;++i){uint8_t c;if(*p>=end)return false;c=b[(*p)++];if(i==4&&(c&0xf8))return false;value|=(uint32_t)(c&127)<<(i*7);if(!(c&128)){if(i&&c==0)return false;*v=value;return true;}}return false;}

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 uint64_t end,p=10,name_at,data;uint32_t count,len,index;unsigned kind=99,i;char label[40];static const char *names[]={"Microsoft.Xna.Framework.Content.Texture2DReader","Microsoft.Xna.Framework.Content.StringReader","Microsoft.Xna.Framework.Content.ByteReader","Microsoft.Xna.Framework.Content.Int32Reader","Microsoft.Xna.Framework.Content.SingleReader"};
 if(n<10||xx_rt_memcmp(b,"XNB",3)||(b[3]!='w'&&b[3]!='x'&&b[3]!='m')||b[4]!=5||b[5]&~1U||(end=xx_data_get_u32(b+6, 4, 0, false))<10||end>n||!var7(b,&p,end,&count)||count!=1||!var7(b,&p,end,&len)||!len||len>1024||!span(p,(uint64_t)len+4,end))return false;
 name_at=p;for(i=0;i<5;++i){size_t l=xx_rt_strlen(names[i]);if(len>=l&&!xx_rt_memcmp(b+p,names[i],l)&&(len==l||b[p+l]==','))kind=i;}if(kind==99)return false;for(i=0;i<len;++i)if(b[name_at+i]<32||b[name_at+i]>126)return false;p+=len;if(xx_data_get_u32(b+p, 4, 0, false))return false;p+=4;if(!var7(b,&p,end,&count)||count||!var7(b,&p,end,&index)||index!=1)return false;data=p;
 if(kind==0){uint32_t format,w,h,mips;if(!span(p,16,end)||(format=xx_data_get_u32(b+p, 4, 0, false))>3||!(w=xx_data_get_u32(b+p+4, 4, 0, false))||w>8192||!(h=xx_data_get_u32(b+p+8, 4, 0, false))||h>8192||(uint64_t)w*h>16777216||!(mips=xx_data_get_u32(b+p+12, 4, 0, false))||mips>16)return false;p+=16;
 for(i=0;i<mips;++i){uint64_t bytes=(uint64_t)w*h*(format?2:4);if(stop(pd)||!span(p,4,end)||xx_data_get_u32(b+p, 4, 0, false)!=bytes||!span(p+4,bytes,end))return false;p+=4;xx_rt_snprintf(label,sizeof(label),"mip-%u.bin",i);if(!emit(f,s,label,p,bytes,end))return false;p+=bytes;if(w==1&&h==1&&i+1<mips)return false;w=w>1?w/2:1;h=h>1?h/2:1;}}
 else {if(kind==1){if(!var7(b,&p,end,&len)||!span(p,len,end))return false;data=p;p+=len;}else{len=kind==2?1:4;if(!span(p,len,end)||(kind==4&&!finite32(b+p,false)))return false;p+=len;}if(!emit(f,s,"value.bin",data,len,end))return false;}
 if(p!=end) {return false; } s->size=(int64_t)end;return true;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<3 || !pm_read(f,0,probe,3) || xx_rt_memcmp(probe,"\x58\x4e\x42",3)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_xna_xnb_init(xx_xna_xnb *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_XNA_XNB,"xnb");} }
xx_xna_xnb *xx_xna_xnb_create(xx_io_device *d,int64_t b) {xx_xna_xnb *r=(xx_xna_xnb *)xx_mem_alloc(sizeof(*r));if(r)xx_xna_xnb_init(r,d,b);return r;}
void xx_xna_xnb_destroy(xx_xna_xnb *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_xna_xnb_free(xx_xna_xnb *r) {if(r){xx_xna_xnb_destroy(r);xx_mem_free(r);}}
bool xx_xna_xnb_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_xna_xnb_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
