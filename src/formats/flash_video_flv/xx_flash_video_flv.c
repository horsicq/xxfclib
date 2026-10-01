/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/flv.h
 * Classic FLV1 with audio/video flags, complete11-byte tag framing and every PreviousTagSize backlink. Audio/video packet headers checked and AMF0 script values recursively parsed. Up to4095 tags,24 AMF levels/65536 values,64MiB file. Exports original encoded tag payloads; codec decoding, playback, encrypted/filter tags, AMF3 and enhanced FLV unsupported.
 */
#include "xxfclib/formats/flash_video_flv/xx_flash_video_flv.h"
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

static bool amf(const uint8_t *b,uint64_t *p,uint64_t end,unsigned depth,unsigned *items,xx_pd_struct *pd) {
 uint8_t t;uint32_t count,i;if(depth>24||++*items>65536||stop(pd)||*p>=end)return false;t=b[(*p)++];switch(t){
 case 0:if(!span(*p,8,end)||!finite64(b+*p,true))return false;*p+=8;return true;
 case 1:if(*p>=end||b[(*p)++]>1)return false;return true;
 case 2:case 12:if(!span(*p,t==2?2:4,end))return false;count=t==2?pm_be16(b+*p):pm_be32(b+*p);*p+=t==2?2:4;if(!span(*p,count,end))return false;*p+=count;return true;
 case 3:case 8:if(t==8){if(!span(*p,4,end)||pm_be32(b+*p)>65536)return false;*p+=4;}while(span(*p,3,end)){count=pm_be16(b+*p);*p+=2;if(!count&&b[*p]==9){++*p;return true;}if(!span(*p,count,end))return false;*p+=count;if(!amf(b,p,end,depth+1,items,pd))return false;}return false;
 case 5:case 6:return true;
 case 10:if(!span(*p,4,end)||(count=pm_be32(b+*p))>65536)return false;*p+=4;for(i=0;i<count;++i)if(!amf(b,p,end,depth+1,items,pd))return false;return true;
 case 11:if(!span(*p,10,end)||!finite64(b+*p,true))return false;*p+=10;return true;default:return false;}
}

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 uint64_t at,header;unsigned tags=0;bool audio=false,video=false;char label[40];if(n<13||xx_rt_memcmp(b,"FLV",3)||b[3]!=1||(b[4]&~5U)||!b[4]||(header=pm_be32(b+5))<9||header>65536||!span(header,4,n)||pm_be32(b+header))return false;at=header+4;
 while(at<n){uint32_t len;uint8_t type;if(stop(pd)||++tags>4095||!span(at,15,n)||(len=be24(b+at+1))==0||!span(at+11,(uint64_t)len+4,n)||be24(b+at+8)||pm_be32(b+at+11+len)!=len+11)return false;type=b[at];
 if(type==8){uint8_t codec=b[at+11]>>4;if(!(b[4]&4)||codec==9||codec>11||len<2||(codec==10&&(len<3||b[at+12]>1)))return false;audio=true;}
 else if(type==9){uint8_t first=b[at+11],codec=first&15,frame=first>>4;if(!(b[4]&1)||codec<2||codec>7||frame<1||frame>5||len<2||(codec==7&&(len<5||b[at+12]>2)))return false;video=true;}
 else if(type==18){uint64_t p=at+11,e=p+len;unsigned items=0;if(b[p]!=2||!amf(b,&p,e,0,&items,pd)||!amf(b,&p,e,0,&items,pd)||p!=e)return false;}else return false;
 xx_rt_snprintf(label,sizeof(label),"tag-%u-%s.bin",tags-1,type==8?"audio":type==9?"video":"script");if(!emit(f,s,label,at+11,len,n))return false;at+=15+len;}
 if(!tags||((b[4]&4)&&!audio)||((b[4]&1)&&!video))return false;s->size=(int64_t)n;return true;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<3 || !pm_read(f,0,probe,3) || xx_rt_memcmp(probe,"\x46\x4c\x56",3)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_flash_video_flv_init(xx_flash_video_flv *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_FLASH_VIDEO_FLV,"flv");} }
xx_flash_video_flv *xx_flash_video_flv_create(xx_io_device *d,int64_t b) {xx_flash_video_flv *r=(xx_flash_video_flv *)xx_mem_alloc(sizeof(*r));if(r)xx_flash_video_flv_init(r,d,b);return r;}
void xx_flash_video_flv_destroy(xx_flash_video_flv *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_flash_video_flv_free(xx_flash_video_flv *r) {if(r){xx_flash_video_flv_destroy(r);xx_mem_free(r);}}
bool xx_flash_video_flv_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_flash_video_flv_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
