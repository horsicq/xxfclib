/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/ariya/FastLZ/blob/master/examples/6pack.c */
#include "xxfclib/formats/fastlz_sixpack/xx_fastlz_sixpack.h"
#include "../common/xx_container_codec_helpers.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint8_t *out = NULL;
    uint64_t at = 8, n, raw, body, want = 0, made = 0, total = 0;
    unsigned count = 0;
    char name[96];
    bool active = false, ok = false;
    if (!blob_load(f, &b, pd)) {
        return false;
    }
    BLOB_NEED(blob_span(&b, 0, 8) && !xx_rt_memcmp(b.p,
                                                   "\x89"
                                                   "6PK\r\n\x1a\n",
                                                   8));
    BLOB_NEED(blob_add(f, s, &b, "sixpack-header", 0, 8));
    while (at < b.n) {
        uint16_t id, opt;
        BLOB_NEED(blob_span(&b, at, 16) && ++count <= 4094);
        id = xx_data_get_u16(b.p + (size_t)at, 2, 0, false);
        opt = xx_data_get_u16(b.p + (size_t)at + 2, 2, 0, false);
        n = xx_data_get_u32(b.p + (size_t)at + 4, 4, 0, false);
        raw = xx_data_get_u32(b.p + (size_t)at + 12, 4, 0, false);
        body = at + 16;
        BLOB_NEED(blob_span(&b, body, n) && xx_adler32(b.p + (size_t)body, (size_t)n) == xx_data_get_u32(b.p + (size_t)at + 8, 4, 0, false));
        if (id == 1) {
            uint16_t namesize;
            BLOB_NEED(!active && !opt && !raw && n >= 11);
            want = xx_data_get_u64(b.p + (size_t)body, 8, 0, false);
            namesize = xx_data_get_u16(b.p + (size_t)body + 8, 2, 0, false);
            BLOB_NEED(namesize && n == (uint64_t)namesize + 10 && b.p[(size_t)(body + n - 1)] == 0 && !xx_rt_memchr(b.p + (size_t)body + 10, 0, namesize - 1) &&
                      bounded_utf8(b.p + (size_t)body + 10, namesize - 1, pd) && want <= 67108864 - total);
            xx_rt_snprintf(name, sizeof(name), "%.*s", (int)(namesize - 1), b.p + (size_t)body + 10);
            out = (uint8_t *)xx_mem_alloc((size_t)(want ? want : 1));
            BLOB_NEED(out);
            made = 0;
            active = true;
            total += want;
        } else {
            BLOB_NEED(id == 17 && active && opt < 2 && n && raw && raw <= 131072 && record_span(made, raw, want));
            if (!opt) {
                BLOB_NEED(n == raw);
                xx_rt_memcpy(out + (size_t)made, b.p + (size_t)body, (size_t)raw);
            } else BLOB_NEED(container_codec_lz(b.p + (size_t)body, n, out + (size_t)made, raw, pd, 1));
            made += raw;
        }
        at = body + n;
        if (active && made == want) {
            BLOB_NEED(container_codec_mem(f, s, name, &out, want));
            active = false;
        }
    }
    BLOB_NEED(!active && s->count > 1);
    s->size = (int64_t)b.n;
    ok = true;
done:
    if (out) xx_mem_free(out);
    xx_mem_free(b.p);
    return ok;
}
void xx_fastlz_sixpack_init(xx_fastlz_sixpack *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_FASTLZ_SIXPACK, "6pk");
    }
}
xx_fastlz_sixpack *xx_fastlz_sixpack_create(xx_io_device *d, int64_t b)
{
    xx_fastlz_sixpack *r = (xx_fastlz_sixpack *)xx_mem_alloc(sizeof(*r));
    if (r) xx_fastlz_sixpack_init(r, d, b);
    return r;
}
void xx_fastlz_sixpack_destroy(xx_fastlz_sixpack *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_fastlz_sixpack_free(xx_fastlz_sixpack *r)
{
    if (r) {
        xx_fastlz_sixpack_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_fastlz_sixpack_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_fastlz_sixpack_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
