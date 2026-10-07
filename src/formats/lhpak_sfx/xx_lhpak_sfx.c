/* SPDX-License-Identifier: MIT. Original bounded Amiga archive decoder from documented wire facts. */
#include "xxfclib/formats/lhpak_sfx/xx_lhpak_sfx.h"
#include "../xx_archive_wrappers.h"
static bool wrap_parse(Abstractformat *f,pm_stream *s,ac_blob *b){static const uint32_t words[5]={0x4c534658,0x2c790000,0x01144aaa,0x00ac6600,0x001841ea};uint32_t at,offset;
 if(b->n<128U||!aw_stub(b,words,false)) {return false; } offset=xx_data_get_u32(b->p+20, 4, 0, true);if(offset>(b->n-128U)/4U)return false;at=128U+offset*4U;
 for(;;){uint32_t size,done=0,total_packed=0,sum=0,entry_at=at,header;uint16_t block=0,initial;uint8_t *out;char name[96];bool final=false;
  if(!ac_poll(b)||!ac_span(b,at,44)) {return false; } size=xx_data_get_u32(b->p+at+24, 4, 0, true);header=44U+4U*(b->p[at+42]+b->p[at+43]);
  if(!ac_span(b,at,header)||!ac_name(name,sizeof(name),b->p+at+44,4U*b->p[at+42])) {return false; } for(uint32_t i=0;i<header;i+=4)sum+=xx_data_get_u32(b->p+at+i, 4, 0, true);if(sum)return ac_error(b,"LhPak header checksum failed");
  initial=xx_data_get_u16(b->p+at+6, 2, 0, true);out=ac_alloc(b,size);if(!out)return false;
  do{uint32_t skip=xx_data_get_u32(b->p+entry_at, 4, 0, true),packed=xx_data_get_u16(b->p+entry_at+4, 2, 0, true),plain=xx_data_get_u32(b->p+entry_at+8, 4, 0, true),crc=xx_data_get_u32(b->p+entry_at+12, 4, 0, true),data_at=entry_at+header;
   if(!ac_poll(b)||xx_data_get_u16(b->p+entry_at+6, 2, 0, true)!=(uint16_t)(initial+block)||!plain||plain>size-done||!ac_span(b,data_at,packed)||crc!=ac_crc32(b->p+data_at,packed,0)||!aw_zoom(b,b->p+data_at,packed,out+done,plain)){ac_release(b,out,size);return false;}
   done+=plain;total_packed+=packed;final=skip==0;if(final){at=data_at+packed;if(done!=size){ac_release(b,out,size);return false;}}
   else{if(skip<header||packed>skip-header||skip>b->n-entry_at){ac_release(b,out,size);return false;}at=entry_at+skip;}
   if(done<size){if(!ac_span(b,at,20)){ac_release(b,out,size);return false;}sum=0;for(unsigned i=0;i<20;i+=4)sum+=xx_data_get_u32(b->p+at+i, 4, 0, true);if(sum){ac_release(b,out,size);return false;}entry_at=at;header=20;++block;}
  }while(done<size);
  if(!ac_memory(f,s,b,name,out,size,total_packed,1)) {return false; } if(final)return aw_tail(b,at);
 }
}
AC_PARSE(wrap_parse)
AC_DEFINE(lhpak_sfx,XX_FILE_TYPE_LHPAK_SFX,"sfx")
