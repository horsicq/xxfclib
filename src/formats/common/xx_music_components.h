/* SPDX-License-Identifier: MIT. Private bounded music component helpers.
 * Original component bytes remain encoded; grammars belong to their readers.
 */
#ifndef XX_MUSIC_COMPONENTS_H
#define XX_MUSIC_COMPONENTS_H
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"
typedef struct music_blob {
    uint8_t *p;
    uint64_t n, work;
    xx_pd_struct *pd;
} music_blob;
typedef struct music_range {
    uint64_t at, n;
} music_range;
static bool music_stop(music_blob *b) { return b->pd && xx_pd_is_stopped(b->pd); }
static bool music_work(music_blob *b, uint64_t n) {
    if (music_stop(b) || n > 2000000 - b->work)
        return false;
    b->work += n;
    return true;
}
static bool music_span(music_blob *b, uint64_t at, uint64_t n) {
    return !music_stop(b) && at <= b->n && n <= b->n - at;
}
static bool music_load(Abstractformat *f, music_blob *b, xx_pd_struct *pd) {
    int64_t n = pm_available(f);
    uint64_t at = 0;
    b->pd = pd;
    if (n < 1 || n > 33554432 || music_stop(b))
        return false;
    b->n = (uint64_t)n;
    b->p = (uint8_t *)xx_mem_alloc((size_t)n);
    if (!b->p)
        return false;
    while (at < b->n) {
        size_t z = b->n - at > 65536 ? 65536 : (size_t)(b->n - at);
        if (!music_span(b, at, z) || !pm_read(f, (int64_t)at, b->p + (size_t)at, z))
            return false;
        at += z;
    }
    return !music_stop(b);
}
static XXFC_MAYBE_UNUSED bool music_tag(music_blob *b, uint64_t at, const char *tag, size_t n) {
    return music_span(b, at, n) && !xx_rt_memcmp(b->p + (size_t)at, tag, n);
}
static bool music_emit(Abstractformat *f, pm_stream *s, music_blob *b, const char *name, uint64_t at, uint64_t n) {
    return n && s->count < 4096 && music_span(b, at, n) && pm_add(f, s, name, (int64_t)at, (int64_t)n);
}
static XXFC_MAYBE_UNUSED bool music_zero_each(music_blob *b, uint64_t at, uint64_t n) {
    uint64_t i;
    if (!music_span(b, at, n))
        return false;
    for (i = 0; i < n; ++i) {
        if (!music_work(b, 1) || b->p[(size_t)(at + i)])
            return false;
    }
    return true;
}
static XXFC_MAYBE_UNUSED bool music_claim(music_blob *b, music_range *ranges, unsigned *count, uint64_t at, uint64_t n,
                                          bool shared) {
    unsigned i;
    if (!n || !music_span(b, at, n) || *count >= 1024)
        return false;
    for (i = 0; i < *count; ++i) {
        if (!music_work(b, 1))
            return false;
        if (at == ranges[i].at && n == ranges[i].n && shared)
            return true;
        if (at < ranges[i].at + ranges[i].n && ranges[i].at < at + n)
            return false;
    }
    ranges[*count].at = at;
    ranges[(*count)++].n = n;
    return true;
}
static XXFC_MAYBE_UNUSED bool music_zstring_any(music_blob *b, uint64_t *at, uint64_t end, uint64_t cap) {
    uint64_t start = *at;
    while (*at < end && *at - start < cap) {
        if (!music_span(b, *at, 1) || !music_work(b, 1))
            return false;
        if (!b->p[(size_t)(*at)++])
            return true;
    }
    return false;
}
static XXFC_MAYBE_UNUSED bool music_zstring_short(music_blob *b, uint64_t *at, uint64_t end) {
    uint64_t start = *at;
    while (*at < end && *at - start < 4096) {
        if (!music_span(b, *at, 1) || !music_work(b, 1))
            return false;
        if (!b->p[(size_t)(*at)++])
            return true;
    }
    return false;
}
static XXFC_MAYBE_UNUSED bool music_float32(music_blob *b, uint64_t at, bool positive) {
    uint32_t u;
    if (!music_span(b, at, 4))
        return false;
    u = xx_data_get_u32(b->p + (size_t)at, 4, 0, false);
    return !(u & 0x80000000U) && (u & 0x7f800000U) != 0x7f800000U && (!positive || (u & 0x7fffffffU));
}
static XXFC_MAYBE_UNUSED bool music_vlq(music_blob *b, uint64_t *at, uint32_t *value) {
    unsigned i;
    uint32_t v = 0;
    for (i = 0; i < 4; ++i) {
        uint8_t c;
        if (!music_span(b, *at, 1) || !music_work(b, 1))
            return false;
        c = b->p[(size_t)(*at)++];
        v = (v << 7) | (c & 127);
        if (!(c & 128)) {
            *value = v;
            return true;
        }
    }
    return false;
}
static XXFC_MAYBE_UNUSED bool music_zero_block(music_blob *b, uint64_t at, uint64_t n) {
    uint64_t i;
    if (!music_span(b, at, n) || !music_work(b, n))
        return false;
    for (i = 0; i < n; ++i)
        if (b->p[(size_t)(at + i)])
            return false;
    return true;
}
static XXFC_MAYBE_UNUSED bool music_hex(music_blob *b, uint64_t at, unsigned width, uint32_t *out) {
    unsigned i;
    uint32_t v = 0;
    if (!width || width > 8 || !music_span(b, at, width))
        return false;
    for (i = 0; i < width; ++i) {
        uint8_t c = b->p[(size_t)at + i];
        unsigned d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else
            return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}
static XXFC_MAYBE_UNUSED bool music_ascii_zstring(music_blob *b, uint64_t *at, uint64_t end, uint64_t cap) {
    uint64_t start = *at;
    while (*at < end && *at - start <= cap) {
        uint8_t c;
        if (!music_span(b, *at, 1) || !music_work(b, 1))
            return false;
        c = b->p[(size_t)(*at)++];
        if (!c)
            return true;
        if (c < 32 || c > 126)
            return false;
    }
    return false;
}
#define MUSIC_NEED(x)                                                                                                  \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            goto done;                                                                                                 \
    } while (0)
#endif
