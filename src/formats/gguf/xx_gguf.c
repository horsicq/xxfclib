/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
/* Primary: https://github.com/ggml-org/ggml/blob/master/docs/gguf.md */
#include "xxfclib/formats/gguf/xx_gguf.h"
#include "xxfclib/data/xx_data.h"
#include "../common/xx_security_framing.h"

static bool string(memory_blob *b, uint64_t *p, uint64_t end, uint64_t *v, uint64_t *n)
{
    if (!protocol_take(b, p, end, 8)) return false;
    *n = xx_data_get_u64(b->p + (size_t)*p - 8, 8, 0, false);
    *v = *p;
    return *n <= 65536 && protocol_take(b, p, end, *n) && serialized_utf(b, *v, *n);
}
static bool value(memory_blob *b, uint64_t *p, uint32_t type, unsigned depth, unsigned *work)
{
    static const unsigned width[] = {1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8};
    if (++*work > 65536 || depth > 1 || type > 12) return false;
    if (type == 8) {
        uint64_t v, n;
        return string(b, p, b->n, &v, &n);
    }
    if (type == 9) {
        if (!protocol_take(b, p, b->n, 12)) return false;
        uint32_t item = xx_data_get_u32(b->p + (size_t)*p - 12, 4, 0, false);
        uint64_t n = xx_data_get_u64(b->p + (size_t)*p - 8, 8, 0, false);
        if (item == 9 || item > 12 || n > 4096) return false;
        for (uint64_t i = 0; i < n; ++i)
            if (!value(b, p, item, depth + 1, work)) return false;
        return true;
    }
    if (!protocol_take(b, p, b->n, width[type])) return false;
    return type != 7 || b->p[(size_t)*p - 1] <= 1;
}
typedef struct gg_tensor {
    uint64_t offset, bytes;
} gg_tensor;
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    bool ok = false;
    gg_tensor *t = NULL;
    uint64_t at = 24, align = 32, comparebytes = 0;
    unsigned work = 0, candidates = 0;
    bool architecture = false;
    uint64_t keys[1024], lens[1024];
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n >= 24 && !xx_rt_memcmp(b.p, "GGUF", 4) && xx_data_get_u32(b.p + 4, 4, 0, false) == 3);
    uint64_t nt = xx_data_get_u64(b.p + 8, 8, 0, false), nk = xx_data_get_u64(b.p + 16, 8, 0, false);
    BLOB_NEED(nt > 0 && nt <= 1024 && nk > 0 && nk <= 1024);
    t = (gg_tensor *)xx_mem_alloc((size_t)nt * sizeof(*t));
    BLOB_NEED(t && blob_add(f, s, &b, "gguf-header", 0, 24));
    for (uint64_t i = 0; i < nk; ++i) {
        uint64_t start = at, v, n;
        BLOB_NEED(string(&b, &at, b.n, &v, &n) && n > 0);
        for (uint64_t k = 0; k < i; ++k) BLOB_NEED(security_distinct(&b, keys[k], lens[k], v, n, &candidates, &comparebytes));
        keys[i] = v;
        lens[i] = n;
        BLOB_NEED(protocol_take(&b, &at, b.n, 4));
        uint32_t type = xx_data_get_u32(b.p + (size_t)at - 4, 4, 0, false);
        if (protocol_eq(&b, v, n, "general.architecture")) {
            BLOB_NEED(type == 8 && blob_span(&b, at, 8) && xx_data_get_u64(b.p + (size_t)at, 8, 0, false) > 0);
            architecture = true;
        }
        if (protocol_eq(&b, v, n, "general.alignment")) {
            BLOB_NEED(type == 4 && blob_span(&b, at, 4));
            align = xx_data_get_u32(b.p + (size_t)at, 4, 0, false);
            BLOB_NEED(align >= 1 && align <= 4096 && !(align & (align - 1)));
        }
        BLOB_NEED(value(&b, &at, type, 0, &work) && blob_add(f, s, &b, "metadata-entry", start, at - start));
    }
    BLOB_NEED(architecture);
    for (uint64_t i = 0; i < nt; ++i) {
        uint64_t start = at, v, n, count = 1;
        BLOB_NEED(string(&b, &at, b.n, &v, &n) && n > 0 && n <= 64);
        for (uint64_t k = 0; k < i; ++k) BLOB_NEED(security_distinct(&b, keys[k], lens[k], v, n, &candidates, &comparebytes));
        keys[i] = v;
        lens[i] = n;
        BLOB_NEED(protocol_take(&b, &at, b.n, 4));
        uint32_t dim = xx_data_get_u32(b.p + (size_t)at - 4, 4, 0, false);
        BLOB_NEED(dim >= 1 && dim <= 4);
        for (unsigned k = 0; k < dim; ++k) {
            BLOB_NEED(protocol_take(&b, &at, b.n, 8));
            uint64_t d = xx_data_get_u64(b.p + (size_t)at - 8, 8, 0, false);
            BLOB_NEED(d > 0 && d <= 67108864 && count <= 67108864 / d);
            count *= d;
        }
        BLOB_NEED(protocol_take(&b, &at, b.n, 12));
        uint32_t type = xx_data_get_u32(b.p + (size_t)at - 12, 4, 0, false);
        unsigned width = type == 0 ? 4 : type == 1 ? 2 : type == 24 ? 1 : type == 25 ? 2 : type == 26 ? 4 : type == 27 ? 8 : type == 28 ? 8 : 0;
        BLOB_NEED(width && count <= 67108864 / width);
        t[i].bytes = count * width;
        t[i].offset = xx_data_get_u64(b.p + (size_t)at - 8, 8, 0, false);
        BLOB_NEED(!(t[i].offset & (align - 1)) && blob_add(f, s, &b, "tensor-directory", start, at - start));
    }
    BLOB_NEED(serialized_pad(&b, &at, align));
    uint64_t base = at, end = 0;
    for (uint64_t k = 0; k < nt; ++k) {
        uint64_t least = UINT64_MAX, index = nt;
        for (uint64_t i = 0; i < nt; ++i)
            if (t[i].offset < least) {
                least = t[i].offset;
                index = i;
            }
        BLOB_NEED(index < nt && least == end && blob_add(f, s, &b, "stored-tensor", base + least, t[index].bytes));
        end = least + t[index].bytes;
        t[index].offset = UINT64_MAX;
        if (k + 1 < nt) {
            uint64_t pad = (align - (end & (align - 1))) & (align - 1);
            BLOB_NEED(blob_zero(&b, base + end, pad));
            end += pad;
        }
    }
    if (base + end != b.n) {
        uint64_t padding = (align - (end & (align - 1))) & (align - 1);
        BLOB_NEED(padding > 0 && base + end + padding == b.n && blob_zero(&b, base + end, padding));
    }
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(t);
    xx_mem_free(b.p);
    return ok;
}

void xx_gguf_init(xx_gguf *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GGUF, "bin");
    }
}
xx_gguf *xx_gguf_create(xx_io_device *d, int64_t b)
{
    xx_gguf *r = (xx_gguf *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gguf_init(r, d, b);
    return r;
}
void xx_gguf_destroy(xx_gguf *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gguf_free(xx_gguf *r)
{
    if (r) {
        xx_gguf_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gguf_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gguf_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
