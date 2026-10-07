/* SPDX-License-Identifier: MIT. Original bounded Amiga archive decoder from documented wire facts. */
#include "xxfclib/formats/s_omni/xx_s_omni.h"
#include "../xx_archive_wrappers.h"
static bool wrap_parse(Abstractformat *f,pm_stream *s,ac_blob *b){static const uint32_t words[5]={0x42adfff4,0x00044eae,0x4aac00ac,0x67026014,0x41ec005c};uint32_t at,tag;uint16_t mode;
 if(!aw_stub(b,words,false)||!ac_span(b,0x208,4)) {return false; } tag=xx_data_get_u32(b->p+0x208, 4, 0, true);at=tag==0x51cdfffc?0x7a8:tag==0x76002c7a?0x744:0;if(!at||!ac_span(b,at,2))return false;
 mode=xx_data_get_u16(b->p+at, 2, 0, true);at+=2;if(mode==1){uint16_t n;if(!ac_span(b,at,2))return false;n=xx_data_get_u16(b->p+at, 2, 0, true);at+=2;if(!ac_span(b,at,n))return false;at+=n;}
 while(ac_span(b,at,2)){uint16_t n=xx_data_get_u16(b->p+at, 2, 0, true);uint32_t packed;char name[96];at+=2;if(!ac_poll(b))return false;if(!n)return s->count&&aw_tail(b,at);
  if(!ac_span(b,at,n)||!ac_name(name,sizeof(name),b->p+at,n)) {return false; } at+=n;if(!ac_span(b,at,4))return false;packed=xx_data_get_u32(b->p+at, 4, 0, true);at+=4;
  if(!ac_span(b,at,packed)||!ac_span(b,at+packed,2)||!aw_payload(f,s,b,name,at,packed)) {return false; } at+=packed+2;
 }return false;
}
AC_PARSE(wrap_parse)
AC_DEFINE(s_omni,XX_FILE_TYPE_S_OMNI,"sfx")
