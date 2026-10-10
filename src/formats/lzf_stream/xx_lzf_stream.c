/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://github.com/nemequ/liblzf/blob/master/lzf.c */
#include "xxfclib/formats/lzf_stream/xx_lzf_stream.h"
#include "../common/xx_container_codec_helpers.h"

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint8_t *out = NULL;
    uint64_t at = 0, n, raw, h, total = 0;
    unsigned count = 0;
    bool ok = false;
    if (!blob_load(f, &b, pd)) {
        return false;
    }
    BLOB_NEED(blob_span(&b, 0, 5) && !xx_rt_memcmp(b.p, "ZV", 2));
    out = (uint8_t *)xx_mem_alloc(67108864);
    BLOB_NEED(out);
    while (at < b.n) {
        if (!b.p[(size_t)at]) {
            BLOB_NEED(at + 1 == b.n);
            ++at;
            break;
        }
        BLOB_NEED(blob_span(&b, at, 5) && !xx_rt_memcmp(b.p + (size_t)at, "ZV", 2) && ++count <= 4094);
        h = b.p[(size_t)at + 2];
        BLOB_NEED(h < 2);
        n = xx_data_get_u16(b.p + (size_t)at + 3, 2, 0, true);
        raw = n;
        if (h) {
            BLOB_NEED(blob_span(&b, at, 7));
            raw = xx_data_get_u16(b.p + (size_t)at + 5, 2, 0, true);
        }
        h = h ? 7 : 5;
        BLOB_NEED(n && raw && blob_span(&b, at + h, n) && raw <= 67108864 - total);
        if (h == 5) xx_rt_memcpy(out + (size_t)total, b.p + (size_t)(at + h), (size_t)raw);
        else BLOB_NEED(container_codec_lz(b.p + (size_t)(at + h), n, out + (size_t)total, raw, pd, 0));
        total += raw;
        at += h + n;
    }
    BLOB_NEED(blob_add(f, s, &b, "block-header", 0, b.p[2] ? 7 : 5) && container_codec_mem(f, s, "decoded-payload", &out, total));
    s->size = (int64_t)b.n;
    ok = true;
done:
    if (out) xx_mem_free(out);
    xx_mem_free(b.p);
    return ok;
}
void xx_lzf_stream_init(xx_lzf_stream *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_LZF_STREAM, "lzf");
    }
}
xx_lzf_stream *xx_lzf_stream_create(xx_io_device *d, int64_t b)
{
    xx_lzf_stream *r = (xx_lzf_stream *)xx_mem_alloc(sizeof(*r));
    if (r) xx_lzf_stream_init(r, d, b);
    return r;
}
void xx_lzf_stream_destroy(xx_lzf_stream *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_lzf_stream_free(xx_lzf_stream *r)
{
    if (r) {
        xx_lzf_stream_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_lzf_stream_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_lzf_stream_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
