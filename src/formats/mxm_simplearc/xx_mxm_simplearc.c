/* SPDX-License-Identifier: MIT. Original bounded Amiga archive decoder from documented wire facts. */
#include "xxfclib/formats/mxm_simplearc/xx_mxm_simplearc.h"
#include "../xx_archive_wrappers.h"
static bool wrap_parse(Abstractformat *f,pm_stream *s,ac_blob *b){static const uint32_t words[5]={0x4eaefdd8,0x4a806700,0x01142e28,0x00a07254,0x2f410020};uint32_t at=0x264;
 if(!aw_stub(b,words,true)) {return false; } while(ac_span(b,at,24)){uint32_t skip=pm_be32(b->p+at),size=pm_be32(b->p+at+4);char name[96];if(!ac_poll(b))return false;
  if(!b->p[at+8]) {return s->count&&aw_tail(b,at+24); } if(!ac_name(name,sizeof(name),b->p+at+8,16)||!ac_span(b,at+24,size))return false;
  if(skip&&(skip<24U||size>skip-24U||skip>b->n-at)) {return false; } if(!ac_emit(f,s,b,name,at+24,size))return false;
  if(!skip) {return aw_tail(b,at+24+size); } at+=skip;
 }return false;
}
AC_PARSE(wrap_parse)
AC_DEFINE(mxm_simplearc,XX_FILE_TYPE_MXM_SIMPLEARC,"sfx")
