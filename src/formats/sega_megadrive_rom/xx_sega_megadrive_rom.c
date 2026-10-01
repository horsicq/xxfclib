/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/ekeeke/Genesis-Plus-GX/master/core/loadrom.c
 * Unwrapped big-endian Mega Drive ROMs with SEGA system header, RAM stack/reset vectors, declared start/end ROM range and16-bit body checksum.512 bytes-16MiB, even length. Exports vectors, cartridge header and original ROM body; SMD/interleaved dumps,32X/Sega CD, bank emulation and execution unsupported.
 */
#include "xxfclib/formats/sega_megadrive_rom/xx_sega_megadrive_rom.h"
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

static bool parse_data(Abstractformat *f,pm_stream *s,const uint8_t *b,uint64_t n,xx_pd_struct *pd) {

 uint32_t stack,entry,sum=0;uint64_t end,at;if(n<514||xx_rt_memcmp(b+256,"SEGA",4)||(stack=pm_be32(b))<0xff0000||stack>0x1000000||stack&1||(entry=pm_be32(b+4))<512||entry&1||pm_be32(b+0x1a0))return false;
 end=1+(uint64_t)pm_be32(b+0x1a4);if(end<514||end>16777216||end>n||(end&1)||entry>=end)return false;for(at=0x100;at<0x18e;++at)if(b[at]<32||b[at]>126)return false;
 for(at=512;at<end;at+=2){if((at&65535)==0&&stop(pd))return false;sum+=pm_be16(b+at);}if((sum&65535)!=pm_be16(b+0x18e))return false;
 if(!emit(f,s,"vectors.bin",0,256,end)||!emit(f,s,"cartridge-header.bin",256,256,end)||!emit(f,s,"rom-body.bin",512,end-512,end))return false;s->size=(int64_t)end;return true;

}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 int64_t available=pm_available(f);uint8_t *b,probe[32];bool result;if(available<1||available>67108864||stop(pd))return false;if(available<260 || !pm_read(f,256,probe,4) || xx_rt_memcmp(probe,"\x53\x45\x47\x41",4)) return false; b=(uint8_t *)xx_mem_alloc((size_t)available);if(!b)return false;
 result=pm_read(f,0,b,(size_t)available)&&parse_data(f,s,b,(uint64_t)available,pd);xx_mem_free(b);return result;
}

void xx_sega_megadrive_rom_init(xx_sega_megadrive_rom *r,xx_io_device *d,int64_t b) { if(r) {xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_SEGA_MEGADRIVE_ROM,"md");} }
xx_sega_megadrive_rom *xx_sega_megadrive_rom_create(xx_io_device *d,int64_t b) {xx_sega_megadrive_rom *r=(xx_sega_megadrive_rom *)xx_mem_alloc(sizeof(*r));if(r)xx_sega_megadrive_rom_init(r,d,b);return r;}
void xx_sega_megadrive_rom_destroy(xx_sega_megadrive_rom *r) {if(r)xx_format_cleanup_extra_parameters(&r->format);}
void xx_sega_megadrive_rom_free(xx_sega_megadrive_rom *r) {if(r){xx_sega_megadrive_rom_destroy(r);xx_mem_free(r);}}
bool xx_sega_megadrive_rom_check_is_valid(Abstractformat *f,xx_pd_struct *pd) {return pm_valid(f,pd);}
bool xx_sega_megadrive_rom_handle_base_info(Abstractformat *f,xx_pd_struct *pd) {return pm_handle(f,pd);}
