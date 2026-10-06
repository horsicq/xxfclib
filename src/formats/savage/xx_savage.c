/* SPDX-License-Identifier: MIT. Original bounded Amiga archive decoder from documented wire facts. */
#include "xxfclib/formats/savage/xx_savage.h"
#include "../xx_archive_wrappers.h"
static bool wrap_parse(Abstractformat *f,pm_stream *s,ac_blob *b){uint32_t n;uint8_t *out;size_t wrote=0;bool ok;uint64_t workspace=32768;
 if(b->n<31||b->p[0]!=29||memcmp(b->p+2,"*SVG*",5)||pm_le32(b->p+11)!=901120U) {return false; } n=pm_le32(b->p+7);if(!n||!ac_span(b,31,n)||!aw_tail(b,31+n))return false;
 out=ac_alloc(b,901120);if(!out)return false;if(workspace>b->limit-b->used){ac_release(b,out,901120);return false;}b->used+=workspace;
 ok=ac_poll(b)&&xx_lzh5_decode_memory(b->p+31,n,out,901120,5,&wrote)&&wrote==901120&&ac_poll(b)&&ac_crc16(out,901120)==pm_le16(b->p+29);b->used-=workspace;
 if(!ok){ac_release(b,out,901120);return ac_error(b,"Savage LH5 decode or disk CRC16 failed");}return ac_memory(f,s,b,"disk.adf",out,901120,n,5);
}
AC_PARSE(wrap_parse)
AC_DEFINE(savage,XX_FILE_TYPE_SAVAGE,"sfx")
