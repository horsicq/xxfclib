/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented primary-format framing; no payload execution.
 */
#include "xxfclib/formats/nintendo_unif/xx_nintendo_unif.h"
#include "../snes_spc/xx_tenth_retro.h"
static bool read_components(Abstractformat *f,pm_stream *s,th_blob *b) {
 const uint8_t *p=b->p;uint32_t a=32,count=0,i,rom_at[32],rom_size[32],checks[32];bool havecrc[32],map=false,prg=false;
 xx_mem_zero(rom_at,sizeof(rom_at));xx_mem_zero(rom_size,sizeof(rom_size));xx_mem_zero(havecrc,sizeof(havecrc));
 if(b->n<41 || xx_rt_memcmp(p,"UNIF",4) || !pm_le32(p+4) || pm_le32(p+4)>7 || !th_zero(p+8,24) || !th_emit(f,s,b,"cartridge-descriptor.bin",0,32)) return false;
 while(a<b->n) {uint32_t z,bank=0;bool rom,crc;char name[64];if(!th_poll(b) || ++count>2048 || !th_range(b,a,8)) return false;z=pm_le32(p+a+4);if(!th_range(b,a+8,z)) return false;for(i=0;i<4;++i) if(p[a+i]<32 || p[a+i]>126) return false;
  rom=!xx_rt_memcmp(p+a,"PRG",3) || !xx_rt_memcmp(p+a,"CHR",3);crc=!xx_rt_memcmp(p+a,"PCK",3) || !xx_rt_memcmp(p+a,"CCK",3);
  if(rom || crc) {unsigned c=p[a+3];if(c>='0' && c<='9') bank=c-'0';else if(c>='A' && c<='F') bank=c-'A'+10;else return false;if(p[a]=='C') bank+=16;
   if(rom) {if(!z || rom_size[bank]) return false;rom_at[bank]=a+8;rom_size[bank]=z;if(bank<16) prg=true;}
   else {if(z!=4 || havecrc[bank]) return false;checks[bank]=pm_le32(p+a+8);havecrc[bank]=true;}
  }
  if(!xx_rt_memcmp(p+a,"MAPR",4)) {if(map || !z || z>256 || p[a+8+z-1]) return false;map=true;}
  if(!xx_rt_memcmp(p+a,"MIRR",4) && (z!=1 || p[a+8]>5)) return false;
  xx_rt_snprintf(name,sizeof(name),"chunk-%u-%c%c%c%c.unif",count-1,p[a],p[a+1],p[a+2],p[a+3]);if(!th_emit(f,s,b,name,a,8+z)) return false;a+=8+z;
 }
 for(i=0;i<32;++i) if(havecrc[i]) {bool okay;uint32_t actual;if(!rom_size[i]) return false;actual=th_crc(b,rom_at[i],rom_size[i],&okay);if(!okay || actual!=checks[i]) return false;}
 if(!map || !prg) return false;s->size=b->n;return true;
}

static bool pm_parse(Abstractformat *f,pm_stream *s,xx_pd_struct *pd) {
 th_blob b;bool ok;if(!th_load(f,&b,pd)) return false;ok=read_components(f,s,&b);xx_mem_free(b.p);return ok;
}
void xx_nintendo_unif_init(xx_nintendo_unif *r,xx_io_device *d,int64_t b) { if(r) { xx_mem_zero(r,sizeof(*r));pm_init(&r->format,d,b,XX_FILE_TYPE_NINTENDO_UNIF,"nintendo_unif"); } }
xx_nintendo_unif *xx_nintendo_unif_create(xx_io_device *d,int64_t b) { xx_nintendo_unif *r=(xx_nintendo_unif *)xx_mem_alloc(sizeof(*r));if(r) xx_nintendo_unif_init(r,d,b);return r; }
void xx_nintendo_unif_destroy(xx_nintendo_unif *r) { if(r) xx_format_cleanup_extra_parameters(&r->format); }
void xx_nintendo_unif_free(xx_nintendo_unif *r) { if(r) {xx_nintendo_unif_destroy(r);xx_mem_free(r);} }
bool xx_nintendo_unif_check_is_valid(Abstractformat *f,xx_pd_struct *pd) { return pm_valid(f,pd); }
bool xx_nintendo_unif_handle_base_info(Abstractformat *f,xx_pd_struct *pd) { return pm_handle(f,pd); }
