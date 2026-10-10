/* SPDX-License-Identifier: MIT. Original LPAQ revision framing; RAM-only licensed helper. */
/* Request POSIX before any header; a strict -std=c11 hides it otherwise. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "xxfclib/formats/lpaq5/xx_lpaq5.h"
#include "../xx_legacy_archive.h"
#include "../xx_archive_codec_pipe.h"
static bool lp_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    uint32_t size;
    uint8_t *out;
    if (b->n < 9 || b->p[0] != 'p' || b->p[1] != 'Q' || b->p[2] != 5 || b->p[3] < '0' || b->p[3] > '9') return false;
    size = xx_data_get_u32(b->p + 4, 4, 0, true);
    if (size > AC_MAX_BYTES) return false;
    out = ac_alloc(b, size);
    if (!out) return false;
    if (!af_decode(b, 6, out, size)) {
        ac_release(b, out, size);
        return false;
    }
    return ac_memory(f, s, b, "decoded.bin", out, size, b->n - 8, 1);
}
AC_PARSE(lp_parse)
AC_DEFINE(lpaq5, XX_FILE_TYPE_LPAQ5, "lpaq5")
