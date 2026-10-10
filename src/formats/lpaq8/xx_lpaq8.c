/* SPDX-License-Identifier: MIT. RAM-only LPAQ8 adapter; retained GPL codec runs separately. */
/* Request POSIX before any header; a strict -std=c11 hides it otherwise. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "xxfclib/formats/lpaq8/xx_lpaq8.h"
#include "../xx_legacy_archive.h"
#include "../xx_archive_codec_pipe.h"
static bool lp_parse(Abstractformat *f, pm_stream *s, ac_blob *b)
{
    xx_lpaq8 *r = (xx_lpaq8 *)f;
    uint32_t size;
    uint8_t *out;
    bool ok;
    if (b->n < 10 || b->p[0] != 'p' || b->p[1] != 'Q' || b->p[2] != 8 || b->p[3] < '0' || b->p[3] > '9' || b->p[8] > 2 ||
        (size = xx_data_get_u32(b->p + 4, 4, 0, true)) > AC_MAX_BYTES)
        return false;
    out = ac_alloc(b, size);
    if (!out) return false;
    ok = af_decode(b, 3, out, size);
    if (!ok) {
        ac_release(b, out, size);
        return false;
    }
    r->level = b->p[3] - '0';
    r->data_mode = b->p[8];
    r->uncompressed_size = size;
    return ac_memory(f, s, b, "decoded.bin", out, size, b->n - 9, 1);
}
AC_PARSE(lp_parse)
AC_DEFINE(lpaq8, XX_FILE_TYPE_LPAQ8, "lpaq8")
bool xx_lpaq8_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_lpaq8_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
int64_t xx_lpaq8_get_format_size(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_size(f, pd);
}
uint64_t xx_lpaq8_get_number_of_archive_records(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_count(f, pd);
}
uint8_t xx_lpaq8_get_level(const xx_lpaq8 *r)
{
    return r ? r->level : 0;
}
uint8_t xx_lpaq8_get_data_mode(const xx_lpaq8 *r)
{
    return r ? r->data_mode : 0;
}
uint32_t xx_lpaq8_get_uncompressed_size(const xx_lpaq8 *r)
{
    return r ? r->uncompressed_size : 0;
}
