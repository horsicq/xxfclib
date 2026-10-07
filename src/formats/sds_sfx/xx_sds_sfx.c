/* SPDX-License-Identifier: MIT. Original bounded Amiga archive decoder from documented wire facts. */
#include "xxfclib/formats/sds_sfx/xx_sds_sfx.h"
#include "../xx_archive_wrappers.h"
static bool wrap_parse(Abstractformat *f,pm_stream *s,ac_blob *b){static const uint32_t words[5]={0xfdd823c0,0x2c404eae,0x0000405a,0x207c0000,0x020c7600};uint32_t at=0x41c;
 if(!aw_stub(b,words,true)) {return false; } while(ac_span(b,at,30)){uint32_t packed,size;uint16_t crc;uint8_t *out;char name[96];if(!ac_poll(b))return false;
  if(!b->p[at]) {return s->count&&aw_tail(b,at+30); } if(!ac_name(name,sizeof(name),b->p+at,20))return false;packed=xx_data_get_u32(b->p+at+20, 4, 0, true);size=xx_data_get_u32(b->p+at+24, 4, 0, true);crc=xx_data_get_u16(b->p+at+28, 2, 0, true);at+=30;
  if(!ac_span(b,at,packed)) {return false; } out=ac_alloc(b,size);if(!out)return false;
  if(!aw_medium(b,b->p+at,packed,out,size)){ac_release(b,out,size);return false;}uint32_t sum=0;for(uint32_t i=0;i<size;++i){if((i&4095U)==0&&!ac_poll(b)){ac_release(b,out,size);return false;}sum+=out[i];}
  if((uint16_t)sum!=crc){ac_release(b,out,size);return ac_error(b,"SDS member checksum failed");}if(!ac_memory(f,s,b,name,out,size,packed,3))return false;at+=packed;
 }return false;
}
AC_PARSE(wrap_parse)
AC_DEFINE(sds_sfx,XX_FILE_TYPE_SDS_SFX,"sfx")
