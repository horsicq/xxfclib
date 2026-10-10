/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
/* Primary layout: https://amazon-ion.github.io/ion-docs/docs/binary.html */
#include "xxfclib/formats/amazon_ion_binary/xx_amazon_ion_binary.h"
#include "../common/xx_container_wire_helpers.h"

static bool io_var(memory_blob *b, uint64_t *at, uint64_t end, uint64_t *v, bool sign)
{
    unsigned count = 0;
    uint8_t c;
    uint64_t n = 0;
    do {
        if (*at >= end || !blob_span(b, *at, 1) || ++count > 10) return false;
        c = b->p[(size_t)(*at)++];
        uint8_t bits = c & (sign && count == 1 ? 63U : 127U);
        if (n > (UINT64_MAX - bits) / 128) return false;
        n = n * 128 + bits;
    } while (!(c & 128));
    *v = n;
    return true;
}
static bool io_value(memory_blob *b, uint64_t *at, uint64_t limit, unsigned depth, unsigned *work, uint64_t symbols)
{
    uint8_t d, t, l;
    uint64_t n, end, p, id;
    if (depth > 32 || ++*work > 262144 || *at >= limit || !blob_span(b, *at, 1)) return false;
    d = b->p[(size_t)(*at)++];
    t = d >> 4;
    l = d & 15;
    if (t == 15) return false;
    if (l == 15) return t <= 13;
    if (t == 1) return l <= 1;
    if (t == 13 && l == 1) {
        if (!io_var(b, at, limit, &n, false)) return false;
    } else if (l == 14) {
        if (!io_var(b, at, limit, &n, false)) return false;
    } else n = l;
    if (!record_span(*at, n, limit) || !blob_span(b, *at, n)) return false;
    end = *at + n;
    if (!t) {
        *at = end;
        return true;
    }
    if (t == 2 || t == 3) {
        if (t == 3 && (!n || container_wire_zero(b, *at, n))) return false;
    } else if (t == 4) {
        if (n != 0 && n != 4 && n != 8) return false;
    } else if (t == 5) {
        if (n && !io_var(b, at, end, &id, true)) return false;
    } else if (t == 6) return false;
    else if (t == 7) {
        if (n > 8) return false;
        id = 0;
        for (p = *at; p < end; ++p) id = (id << 8) | b->p[(size_t)p];
        if (id > symbols) return false;
    } else if (t == 8) {
        if (!serialized_utf(b, *at, n)) return false;
    } else if (t == 9) {
        for (p = *at; p < end; ++p)
            if (b->p[(size_t)p] > 127) return false;
    } else if (t == 10) {
    } else if (t == 11 || t == 12) {
        while (*at < end)
            if (!io_value(b, at, end, depth + 1, work, symbols)) return false;
        return *at == end;
    } else if (t == 13) {
        while (*at < end) {
            if (!io_var(b, at, end, &id, false) || !id || id > symbols || !io_value(b, at, end, depth + 1, work, symbols)) return false;
        }
        return *at == end;
    } else if (t == 14) {
        if (!io_var(b, at, end, &n, false) || !n || !record_span(*at, n, end)) return false;
        p = *at + n;
        while (*at < p)
            if (!io_var(b, at, p, &id, false) || !id || id > symbols) return false;
        if (*at == end || (b->p[(size_t)*at] >> 4) == 14 || (b->p[(size_t)*at] >> 4) == 0 || !io_value(b, at, end, depth + 1, work, symbols)) return false;
        return *at == end;
    } else return false;
    *at = end;
    return true;
}
static bool io_lsts(memory_blob *b, uint64_t start, uint64_t end, uint64_t *symbols)
{
    uint64_t at = start, n, stop, id, count = 0, annotationEnd;
    uint8_t d;
    if (at >= end || (b->p[(size_t)at] >> 4) != 14) return true;
    d = b->p[(size_t)at++];
    if ((d & 15) == 14) {
        if (!io_var(b, &at, end, &n, false)) return false;
    } else n = d & 15;
    if (!record_span(at, n, end) || at + n != end || !io_var(b, &at, end, &n, false) || !n || !record_span(at, n, end)) return false;
    annotationEnd = at + n;
    if (!io_var(b, &at, annotationEnd, &id, false)) return false;
    if (id != 3) return true;
    at = annotationEnd;
    if (at >= end) return false;
    d = b->p[(size_t)at++];
    if ((d >> 4) != 13 || (d & 15) == 15) return false;
    if ((d & 15) == 14 || (d & 15) == 1) {
        if (!io_var(b, &at, end, &n, false)) return false;
    } else n = d & 15;
    if (at + n != end) return false;
    while (at < end) {
        if (!io_var(b, &at, end, &id, false) || id != 7 || at >= end) return false;
        d = b->p[(size_t)at++];
        if ((d >> 4) != 11 || (d & 15) == 15) return false;
        if ((d & 15) == 14) {
            if (!io_var(b, &at, end, &n, false)) return false;
        } else n = d & 15;
        if (!record_span(at, n, end)) return false;
        stop = at + n;
        while (at < stop) {
            d = b->p[(size_t)at++];
            if ((d >> 4) != 8 || (d & 15) == 15) return false;
            if ((d & 15) == 14) {
                if (!io_var(b, &at, stop, &n, false)) return false;
            } else n = d & 15;
            if (!record_span(at, n, stop) || !serialized_utf(b, at, n) || ++count > 4096) return false;
            at += n;
        }
    }
    *symbols = 9 + count;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    memory_blob b;
    uint64_t at = 4, start, symbols = 9;
    unsigned work = 0, values = 0;
    bool ok = false;
    static const uint8_t magic[] = {224, 1, 0, 234};
    if (!blob_load(f, &b, pd)) return false;
    BLOB_NEED(b.n > 4 && !xx_rt_memcmp(b.p, magic, 4) && blob_add(f, s, &b, "ion-version", 0, 4));
    while (at < b.n) {
        start = at;
        BLOB_NEED(io_value(&b, &at, b.n, 0, &work, symbols) && io_lsts(&b, start, at, &symbols) && ++values <= 2048 &&
                  blob_add(f, s, &b, "encoded-ion-value", start, at - start));
    }
    BLOB_NEED(values);
    s->size = (int64_t)b.n;
    ok = true;
done:
    xx_mem_free(b.p);
    return ok;
}

void xx_amazon_ion_binary_init(xx_amazon_ion_binary *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_AMAZON_ION_BINARY, "ion");
    }
}
xx_amazon_ion_binary *xx_amazon_ion_binary_create(xx_io_device *d, int64_t b)
{
    xx_amazon_ion_binary *r = (xx_amazon_ion_binary *)xx_mem_alloc(sizeof(*r));
    if (r) xx_amazon_ion_binary_init(r, d, b);
    return r;
}
void xx_amazon_ion_binary_destroy(xx_amazon_ion_binary *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_amazon_ion_binary_free(xx_amazon_ion_binary *r)
{
    if (r) {
        xx_amazon_ion_binary_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_amazon_ion_binary_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_amazon_ion_binary_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
