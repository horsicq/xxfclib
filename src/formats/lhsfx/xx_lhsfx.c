/* SPDX-License-Identifier: MIT. Original bounded Amiga archive decoder from documented wire facts. */
#include "xxfclib/formats/lhsfx/xx_lhsfx.h"
#include "../xx_archive_wrappers.h"
static bool wrap_parse(Abstractformat *f,pm_stream *s,ac_blob *b){static const uint32_t words[5]={0x43f90000,0x00000318,0x02ec2c79,0x00000004,0x4eaefdd8};uint32_t at=0x918;
 if(!aw_stub(b,words,false)) {return false; } while(ac_span(b,at,4)){uint32_t size,packed;uint8_t *out;char name[96];if(!ac_poll(b))return false;
  if(pm_be32(b->p+at)==UINT32_MAX) {return s->count&&aw_tail(b,at+4); } at+=4;if(!ac_span(b,at,60))return false;size=pm_be32(b->p+at);packed=pm_be32(b->p+at+4);
  if(!b->p[at+12])strcpy(name,"decoded.bin");else if(!ac_name(name,sizeof(name),b->p+at+12,48))return false;at+=60;
  if(!ac_span(b,at,packed)) {return false; } out=ac_alloc(b,size);if(!out)return false;if(!aw_zoom(b,b->p+at,packed,out,size)){ac_release(b,out,size);return false;}
  if(!ac_memory(f,s,b,name,out,size,packed,1)) {return false; } at+=packed;
 }return false;
}
AC_PARSE(wrap_parse)
AC_DEFINE(lhsfx,XX_FILE_TYPE_LHSFX,"sfx")
