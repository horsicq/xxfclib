/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://n64dev.org/n64crc.html
 * Native big-endian N64 images with standard80371240 header, cached entry address and CIC6101/6102 data checksums over the first1MiB at0x1000. File1MiB+4KiB-64MiB in512-byte units. Exports original header, IPL3 boot block and ROM body; byte-swapped images, other CIC checksums, boot-code authentication, emulation and execution unsupported.
 */
#include "xxfclib/formats/nintendo_n64_rom/xx_nintendo_n64_rom.h"
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

static uint32_t rol32(uint32_t n,unsigned bits) {return bits?(n<<bits)|(n>>(32-bits)):n;}

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 uint32_t t1=0xf8ca4ddcU,t2=t1,t3=t1,t4=t1,t5=t1,t6=t1,entry;uint64_t at;uint32_t crc1,crc2;if(n<0x101000||n>67108864||(n&511)||xx_data_get_u32(b, 4, 0, true)!=0x80371240U||(entry=xx_data_get_u32(b+8, 4, 0, true))<0x80000000U||entry>=0x80800000U||(entry&3)||!zero(b+24,8))return false;
 for(at=0x20;at<0x34;++at) {if(b[at]<32||b[at]>126)return false; } for(at=0x1000;at<0x101000;at+=4){uint32_t d=xx_data_get_u32(b+at, 4, 0, true),r=rol32(d,d&31),next=t6+d;if((at&0x3fff)==0&&stop(pd))return false;if(next<t6)++t4;t6=next;t3^=d;t5+=r;if(t2>d)t2^=r;else t2^=t6^d;t1+=t5^d;}
 crc1=t6^t4^t3;crc2=t5^t2^t1;if(xx_data_get_u32(b+16, 4, 0, true)!=crc1||xx_data_get_u32(b+20, 4, 0, true)!=crc2)return false;
 if(!emit(f,s,"header.bin",0,64,n)||!emit(f,s,"ipl3.bin",64,4032,n)||!emit(f,s,"rom-body.bin",4096,n-4096,n)) {return false; } s->size=(int64_t)n;return true;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<4 || !pm_read(f,0,probe,4) || xx_rt_memcmp(probe,"\x80\x37\x12\x40",4)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_nintendo_n64_rom_init(xx_nintendo_n64_rom *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_N64_ROM,"z64");} }
xx_nintendo_n64_rom *xx_nintendo_n64_rom_create(xx_io_device *d,int64_t b) {xx_nintendo_n64_rom *r=(xx_nintendo_n64_rom *)xx_mem_alloc(sizeof(*r));if(r)xx_nintendo_n64_rom_init(r,d,b);return r;}
void xx_nintendo_n64_rom_destroy(xx_nintendo_n64_rom *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_nintendo_n64_rom_free(xx_nintendo_n64_rom *r) {if(r){xx_nintendo_n64_rom_destroy(r);xx_mem_free(r);}}
bool xx_nintendo_n64_rom_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_nintendo_n64_rom_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
