/* SPDX-License-Identifier: MIT. Authenticated GRZipII framing and RAM-only LGPL codec helper. */
#include "xxfclib/formats/grzip/xx_grzip.h"
#include "../xx_legacy_archive.h"
#include "../xx_archive_codec_pipe.h"
static bool gr_parse(Abstractformat *f,pm_stream *s,ac_blob *b){static const uint8_t magic[12]={'G','R','Z','i','p','I','I',0,2,4,':',')'};uint32_t at=12,total=0;uint8_t *out;bool ok;
 if(b->n<12||memcmp(b->p,magic,12)) {return false; } while(at<b->n){uint32_t packed,plain;if(!ac_span(b,at,28)||!ac_poll(b))return false;plain=xx_data_get_u32(b->p+at, 4, 0, false);packed=xx_data_get_u32(b->p+at+16, 4, 0, false);if(!plain||plain>8388096U||plain>AC_MAX_BYTES-total||!ac_span(b,at+28,packed)||~ac_crc32(b->p+at,24,UINT32_MAX)!=xx_data_get_u32(b->p+at+24, 4, 0, false)||~ac_crc32(b->p+at+28,packed,UINT32_MAX)!=xx_data_get_u32(b->p+at+20, 4, 0, false))return false;total+=plain;at+=28U+packed;}
 out=ac_alloc(b,total);if(!out)return false;ok=af_decode(b,4,out,total);if(!ok){ac_release(b,out,total);return false;}return ac_memory(f,s,b,"decoded.bin",out,total,b->n-12,1);
}
AC_PARSE(gr_parse)
AC_DEFINE(grzip,XX_FILE_TYPE_GRZIP,"grz")
