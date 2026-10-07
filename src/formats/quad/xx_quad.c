/* SPDX-License-Identifier: MIT. QUAD 1.12 RAM-only retained-codec adapter. */
#include "xxfclib/formats/quad/xx_quad.h"
#include "../xx_legacy_archive.h"
#include "../xx_archive_codec_pipe.h"
static bool q_parse(Abstractformat *f,pm_stream *s,ac_blob *b){uint32_t size;uint8_t *out;bool ok;if(b->n<9U||(size=xx_data_get_u32(b->p, 4, 0, false))>AC_MAX_BYTES)return false;out=ac_alloc(b,size);if(!out)return false;ok=af_decode(b,1,out,size);if(!ok){ac_release(b,out,size);return false;}return ac_memory(f,s,b,"decoded.bin",out,size,b->n-4U,1);}
AC_PARSE(q_parse)
AC_DEFINE(quad,XX_FILE_TYPE_QUAD,"quad")
