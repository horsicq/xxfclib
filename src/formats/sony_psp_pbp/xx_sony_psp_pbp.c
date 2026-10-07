/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/hrydgard/ppsspp/master/Core/ELF/PBPReader.cpp
 * PSP PBP version1.0 homebrew packages with eight monotonically ordered section offsets. Requires bounded PARAM.SFO key/type/value tables and complete32-bit little-endian MIPS ELF DATA.PSP, no DATA.PSAR. Checks ELF program/section extents and entry point. Exports original section bytes; encrypted PSP/PSAR, ISO interpretation and execution unsupported.
 */
#include "xxfclib/formats/sony_psp_pbp/xx_sony_psp_pbp.h"
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

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 uint32_t offsets[9],i,count;uint64_t sf,end;char label[40];static const char *names[]={"PARAM.SFO","ICON0.PNG","ICON1.PMF","PIC0.PNG","PIC1.PNG","SND0.AT3","DATA.PSP","DATA.PSAR"};
 if(n<40||xx_rt_memcmp(b,"\0PBP",4)||xx_data_get_u32(b+4, 4, 0, false)!=0x10000) {return false; } for(i=0;i<8;++i){offsets[i]=xx_data_get_u32(b+8+i*4, 4, 0, false);if(offsets[i]<40||offsets[i]>n||(i&&offsets[i]<offsets[i-1]))return false;}offsets[8]=(uint32_t)n;if(offsets[0]!=40||offsets[1]-offsets[0]<20||offsets[7]==offsets[6])return false;
 if(offsets[7]!=n) {return false; } sf=offsets[0];end=offsets[1];if(xx_rt_memcmp(b+sf,"\0PSF",4)||xx_data_get_u32(b+sf+4, 4, 0, false)!=0x101||!(count=xx_data_get_u32(b+sf+16, 4, 0, false))||count>256||!span(sf+20,(uint64_t)count*16,end))return false;
 {uint32_t keys=xx_data_get_u32(b+sf+8, 4, 0, false),data=xx_data_get_u32(b+sf+12, 4, 0, false);range r[256];unsigned nr=0;if(keys<20+count*16||data<=keys||sf+data>end)return false;
 for(i=0;i<count;++i){const uint8_t *e=b+sf+20+i*16;uint16_t type=xx_data_get_u16(e+2, 2, 0, false);uint32_t len=xx_data_get_u32(e+4, 4, 0, false),max=xx_data_get_u32(e+8, 4, 0, false),off=xx_data_get_u32(e+12, 4, 0, false);uint64_t key=sf+keys+xx_data_get_u16(e, 2, 0, false);
 if(stop(pd)||key>=sf+data||!cstr(b,&key,sf+data,1024,false)||!len||len>max||!reserve(r,&nr,256,sf+data+off,max,sf+data,end)||(type!=0x4&&type!=0x204&&type!=0x404)||(type==0x404&&len!=4)||(type==0x204&&b[sf+data+off+len-1]))return false;}}
 {const uint8_t *e=b+offsets[6];uint64_t size=offsets[7]-offsets[6],extent=52;uint32_t ph,sh,entry;uint16_t pn,sn;bool found=false;
 if(size<84||xx_rt_memcmp(e,"\x7f" "ELF",4)||e[4]!=1||e[5]!=1||e[6]!=1||xx_data_get_u16(e+16, 2, 0, false)!=2||xx_data_get_u16(e+18, 2, 0, false)!=8||xx_data_get_u32(e+20, 4, 0, false)!=1||xx_data_get_u16(e+40, 2, 0, false)!=52||xx_data_get_u16(e+42, 2, 0, false)!=32||!(pn=xx_data_get_u16(e+44, 2, 0, false))||pn>32||(sn=xx_data_get_u16(e+48, 2, 0, false))>256)return false;
 entry=xx_data_get_u32(e+24, 4, 0, false);ph=xx_data_get_u32(e+28, 4, 0, false);sh=xx_data_get_u32(e+32, 4, 0, false);if(ph<52||!span(ph,(uint64_t)pn*32,size)||(sn&&(xx_data_get_u16(e+46, 2, 0, false)!=40||sh<52||!span(sh,(uint64_t)sn*40,size))))return false;extent=ph+(uint64_t)pn*32;if(sn&&sh+(uint64_t)sn*40>extent)extent=sh+(uint64_t)sn*40;
 for(i=0;i<pn;++i){const uint8_t *p=e+ph+i*32;uint32_t off=xx_data_get_u32(p+4, 4, 0, false),addr=xx_data_get_u32(p+8, 4, 0, false),len=xx_data_get_u32(p+16, 4, 0, false),mem=xx_data_get_u32(p+20, 4, 0, false);if(!span(off,len,size)||len>mem)return false;if(off+(uint64_t)len>extent)extent=off+(uint64_t)len;if(xx_data_get_u32(p, 4, 0, false)==1&&entry>=addr&&(uint64_t)entry-addr<len)found=true;}
 for(i=0;i<sn;++i){const uint8_t *p=e+sh+i*40;if(xx_data_get_u32(p+4, 4, 0, false)!=8){uint32_t off=xx_data_get_u32(p+16, 4, 0, false),len=xx_data_get_u32(p+20, 4, 0, false);if(!span(off,len,size))return false;if(off+(uint64_t)len>extent)extent=off+(uint64_t)len;}}if(!found||extent!=size)return false;}
 for(i=0;i<8;++i) {if(offsets[i+1]>offsets[i]){xx_rt_snprintf(label,sizeof(label),"%s",names[i]);if(!emit(f,s,label,offsets[i],offsets[i+1]-offsets[i],n))return false;} } s->size=(int64_t)n;return true;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<4 || !pm_read(f,0,probe,4) || xx_rt_memcmp(probe,"\x00\x50\x42\x50",4)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_sony_psp_pbp_init(xx_sony_psp_pbp *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SONY_PSP_PBP,"pbp");} }
xx_sony_psp_pbp *xx_sony_psp_pbp_create(xx_io_device *d,int64_t b) {xx_sony_psp_pbp *r=(xx_sony_psp_pbp *)xx_mem_alloc(sizeof(*r));if(r)xx_sony_psp_pbp_init(r,d,b);return r;}
void xx_sony_psp_pbp_destroy(xx_sony_psp_pbp *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_sony_psp_pbp_free(xx_sony_psp_pbp *r) {if(r){xx_sony_psp_pbp_destroy(r);xx_mem_free(r);}}
bool xx_sony_psp_pbp_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_sony_psp_pbp_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
