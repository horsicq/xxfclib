/* SPDX-License-Identifier: MIT. Private checked shared format primitives. */
#ifndef XX_MEMORY_BLOB_H
#define XX_MEMORY_BLOB_H
#include "xx_binary_records.h"
#include "xxfclib/algo/crc/xx_crc.h"
typedef struct memory_blob {
    uint8_t *p;
    uint64_t n;
    xx_pd_struct *pd;
} memory_blob;
static bool blob_load(Abstractformat *f, memory_blob *b, xx_pd_struct *pd)
{
    int64_t n = pm_available(f);
    uint64_t at = 0;
    b->p = NULL;
    b->n = 0;
    b->pd = pd;
    if (n <= 0 || n > 67108864 || binary_stop(pd)) return false;
    b->p = (uint8_t *)xx_mem_alloc((size_t)n);
    if (!b->p) return false;
    b->n = (uint64_t)n;
    while (at < b->n) {
        size_t count = b->n - at > 65536 ? 65536 : (size_t)(b->n - at);
        if (binary_stop(pd) || !pm_read(f, (int64_t)at, b->p + (size_t)at, count)) {
            xx_mem_free(b->p);
            b->p = NULL;
            b->n = 0;
            return false;
        }
        at += count;
    }
    return true;
}
static bool blob_span(const memory_blob *b, uint64_t at, uint64_t n)
{
    return !binary_stop(b->pd) && record_span(at, n, b->n);
}
static bool blob_add(Abstractformat *f, pm_stream *s, memory_blob *b, const char *name, uint64_t at, uint64_t n)
{
    return s->count < 4096 && blob_span(b, at, n) && pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static XXFC_MAYBE_UNUSED bool blob_string(memory_blob *b, uint64_t *at, uint64_t end, unsigned width, bool be, bool empty)
{
    uint64_t n;
    if (!record_span(*at, width, end) || !blob_span(b, *at, width)) return false;
    n = width == 2 ? xx_data_get_u16(b->p + (size_t)*at, 2, 0, be) : xx_data_get_u32(b->p + (size_t)*at, 4, 0, be);
    *at += width;
    if (n > 65536 || (!empty && !n) || !record_span(*at, n, end) || !blob_span(b, *at, n) || !bounded_utf8(b->p + (size_t)*at, (size_t)n, b->pd)) return false;
    *at += n;
    return true;
}
static XXFC_MAYBE_UNUSED bool blob_floats(memory_blob *b, uint64_t at, uint64_t n, unsigned width, bool be)
{
    uint64_t i;
    if (!blob_span(b, at, n) || n % width) return false;
    for (i = 0; i < n; i += width) {
        if (!(i & 65535U) && binary_stop(b->pd)) return false;
        if (width == 4 ? (xx_data_get_u32(b->p + (size_t)(at + i), 4, 0, be) & 0x7f800000U) == 0x7f800000U
                       : !numeric_finite64(xx_data_get_u64(b->p + (size_t)(at + i), 8, 0, be)))
            return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool blob_zero(memory_blob *b, uint64_t at, uint64_t n)
{
    uint64_t i;
    if (!blob_span(b, at, n)) return false;
    for (i = 0; i < n; ++i)
        if (b->p[(size_t)(at + i)]) return false;
    return true;
}
static XXFC_MAYBE_UNUSED uint16_t blob_crc16(const uint8_t *p, size_t n)
{
    return xx_crc16_arc_calc(0, p, n);
}
static XXFC_MAYBE_UNUSED uint32_t blob_crc32(const uint8_t *p, size_t n)
{
    return xx_crc32_calc(0, p, n);
}
static XXFC_MAYBE_UNUSED bool blob_date(const uint8_t *p)
{
    return xx_data_get_u16(p, 2, 0, false) >= 1970 && xx_data_get_u16(p, 2, 0, false) <= 3000 && xx_data_get_u16(p + 2, 2, 0, false) >= 1 &&
           xx_data_get_u16(p + 2, 2, 0, false) <= 12 && xx_data_get_u16(p + 6, 2, 0, false) >= 1 && xx_data_get_u16(p + 6, 2, 0, false) <= 31 &&
           xx_data_get_u16(p + 8, 2, 0, false) < 24 && xx_data_get_u16(p + 10, 2, 0, false) < 60 && xx_data_get_u16(p + 12, 2, 0, false) < 61 &&
           xx_data_get_u16(p + 14, 2, 0, false) < 1000;
}
static XXFC_MAYBE_UNUSED bool blob_ascii(const uint8_t *p, size_t n, bool nul)
{
    size_t i;
    for (i = 0; i < n; ++i)
        if ((!p[i] && !nul) || p[i] > 126 || (p[i] < 32 && p[i] && p[i] != '\n' && p[i] != '\r' && p[i] != '\t')) return false;
    return true;
}
static XXFC_MAYBE_UNUSED bool blob_field(const char *text, const char *key, char *value, size_t cap)
{
    const char *p = text;
    size_t k = xx_rt_strlen(key), n;
    unsigned found = 0;
    while (*p) {
        const char *end = p;
        bool quote = false;
        while (*end) {
            if (*end == '\"') quote = !quote;
            if (*end == ';' && !quote) break;
            ++end;
        }
        if (quote) return false;
        while (p < end && (*p == ' ' || *p == '\r' || *p == '\n')) ++p;
        if ((size_t)(end - p) > k && !xx_rt_memcmp(p, key, k) && (p[k] == ' ' || p[k] == '=')) {
            p += k;
            while (p < end && (*p == ' ' || *p == '=')) ++p;
            n = (size_t)(end - p);
            while (n && p[n - 1] == ' ') --n;
            if (n >= cap || ++found > 1) return false;
            xx_rt_memcpy(value, p, n);
            value[n] = 0;
        }
        p = *end ? end + 1 : end;
    }
    return found == 1;
}
#define BLOB_NEED(condition)         \
    do {                             \
        if (!(condition)) goto done; \
    } while (0)
#endif
