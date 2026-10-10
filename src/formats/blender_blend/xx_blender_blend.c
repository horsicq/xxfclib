/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/blender/blender/blob/main/doc/blender_file_format/mystery_of_the_blend.html
 * Uncompressed classic BLENDER headers, pointer widths32/64 and either byte order, versions250-499. Parses full BHead framing, exactly one bounded SDNA dictionary with
 * all type/name/structure references, and terminating ENDB. Up to4096 blocks and64MiB file. Exports original encoded blocks; DNA field interpretation, pointer
 * relocation, newer blend variants, compression and rendering unsupported.
 */
#include "xxfclib/formats/blender_blend/xx_blender_blend.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "../xx_payload_members.h"
#include "xxfclib/data/xx_data.h"

static __inline bool span(uint64_t a, uint64_t n, uint64_t e)
{
    return a <= e && n <= e - a;
}
static __inline bool stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static __inline bool zero(const uint8_t *b, uint64_t n)
{
    uint64_t i;
    for (i = 0; i < n; ++i)
        if (b[i]) return false;
    return true;
}
static __inline bool finite32(const uint8_t *p, bool be)
{
    return (xx_data_get_u32(p, 4, 0, be) & 0x7f800000U) != 0x7f800000U;
}
static __inline bool finite64(const uint8_t *p, bool be)
{
    return (xx_data_get_u64(p, 8, 0, be) & 0x7ff0000000000000ULL) != 0x7ff0000000000000ULL;
}
static __inline bool floats(const uint8_t *b, uint64_t at, uint64_t count, bool be, uint64_t n)
{
    uint64_t i;
    if (!span(at, count * 4, n)) return false;
    for (i = 0; i < count; ++i)
        if (!finite32(b + at + i * 4, be)) return false;
    return true;
}
static __inline bool emit(Abstractformat *f, pm_stream *s, const char *label, uint64_t a, uint64_t n, uint64_t e)
{
    return span(a, n, e) && s->count < 4096 && pm_add(f, s, label, (int64_t)a, (int64_t)n);
}
static __inline bool cstr(const uint8_t *b, uint64_t *at, uint64_t end, uint64_t maximum, bool empty)
{
    uint64_t start = *at;
    while (*at < end && *at - start <= maximum) {
        uint8_t c = b[(*at)++];
        if (!c) return empty || *at > start + 1;
        if (c < 32 || c == 127) return false;
    }
    return false;
}
typedef struct range {
    uint64_t at, n;
} range;
static __inline bool reserve(range *r, unsigned *nr, unsigned max, uint64_t at, uint64_t n, uint64_t lo, uint64_t end)
{
    unsigned i;
    if (*nr >= max || at < lo || !span(at, n, end)) return false;
    for (i = 0; i < *nr; ++i)
        if (n && r[i].n && at < r[i].at + r[i].n && r[i].at < at + n) return false;
    r[*nr].at = at;
    r[*nr].n = n;
    ++*nr;
    return true;
}
static __inline uint32_t crc32_bytes(const uint8_t *b, uint64_t n)
{
    return xx_crc32_calc(0U, b, (size_t)n);
}

static bool dna(const uint8_t *b, uint64_t at, uint64_t end, bool be, uint32_t *structures, xx_pd_struct *pd)
{
    uint32_t names, types, ns, i, j;
    uint64_t p = at;
    if (!span(p, 12, end) || xx_rt_memcmp(b + p, "SDNANAME", 8)) return false;
    names = xx_data_get_u32(b + p + 8, 4, 0, be);
    p += 12;
    if (!names || names > 65536) return false;
    for (i = 0; i < names; ++i) {
        if (stop(pd) || !cstr(b, &p, end, 256, false)) return false;
    }
    p = (p + 3) & ~3ULL;
    if (!span(p, 8, end) || xx_rt_memcmp(b + p, "TYPE", 4)) {
        return false;
    }
    types = xx_data_get_u32(b + p + 4, 4, 0, be);
    p += 8;
    if (!types || types > 65536) return false;
    for (i = 0; i < types; ++i) {
        if (!cstr(b, &p, end, 256, false)) return false;
    }
    p = (p + 3) & ~3ULL;
    if (!span(p, 4 + (uint64_t)types * 2, end) || xx_rt_memcmp(b + p, "TLEN", 4)) {
        return false;
    }
    p = (p + 4 + (uint64_t)types * 2 + 3) & ~3ULL;
    if (!span(p, 8, end) || xx_rt_memcmp(b + p, "STRC", 4)) {
        return false;
    }
    ns = xx_data_get_u32(b + p + 4, 4, 0, be);
    p += 8;
    if (!ns || ns > 65536) return false;
    for (i = 0; i < ns; ++i) {
        uint32_t count;
        if (stop(pd) || !span(p, 4, end) || xx_data_get_u16(b + p, 2, 0, be) >= types) return false;
        count = xx_data_get_u16(b + p + 2, 2, 0, be);
        p += 4;
        if (!span(p, (uint64_t)count * 4, end)) return false;
        for (j = 0; j < count; ++j)
            if (xx_data_get_u16(b + p + j * 4, 2, 0, be) >= types || xx_data_get_u16(b + p + j * 4 + 2, 2, 0, be) >= names) return false;
        p += (uint64_t)count * 4;
    }
    if (p != end) {
        return false;
    }
    *structures = ns;
    return true;
}

static bool parse_data(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd)
{
    uint64_t at = 12, dna_at = 0, dna_n = 0;
    uint32_t blocks = 0, ns = 0, i;
    unsigned ptr, hs;
    bool be;
    char label[40];
    if (n < 12 || xx_rt_memcmp(b, "BLENDER", 7) || (b[7] != '_' && b[7] != '-') || (b[8] != 'v' && b[8] != 'V') || b[9] < '2' || b[9] > '4' || b[10] < '0' ||
        b[10] > '9' || b[11] < '0' || b[11] > '9' || ((b[9] - '0') * 100 + (b[10] - '0') * 10 + b[11] - '0') < 250)
        return false;
    ptr = b[7] == '-' ? 8 : 4;
    hs = 16 + ptr;
    be = b[8] == 'V';
    while (span(at, hs, n)) {
        uint32_t len = xx_data_get_u32(b + at + 4, 4, 0, be), count = xx_data_get_u32(b + at + 12 + ptr, 4, 0, be);
        if (stop(pd) || ++blocks > 4096 || !span(at + hs, len, n)) return false;
        if (!xx_rt_memcmp(b + at, "ENDB", 4)) {
            if (len || !zero(b + at + 8, hs - 8) || !dna_at || !dna(b, dna_at, dna_at + dna_n, be, &ns, pd)) return false;
            s->size = (int64_t)(at + hs);
            break;
        }
        for (i = 0; i < 4; ++i)
            if (b[at + i] && (b[at + i] < 32 || b[at + i] > 126)) return false;
        if (!len || !count || count > 1000000) {
            return false;
        }
        if (!xx_rt_memcmp(b + at, "DNA1", 4)) {
            if (dna_at || count != 1) return false;
            dna_at = at + hs;
            dna_n = len;
        }
        xx_rt_snprintf(label, sizeof(label), "block-%u.bin", blocks - 1);
        if (!emit(f, s, label, at, hs + len, n)) return false;
        at += hs + len;
    }
    if (!s->size) {
        return false;
    }
    at = 12;
    while (at + hs < (uint64_t)s->size) {
        uint32_t len = xx_data_get_u32(b + at + 4, 4, 0, be);
        if (xx_rt_memcmp(b + at, "DNA1", 4) && xx_data_get_u32(b + at + 8 + ptr, 4, 0, be) >= ns) return false;
        at += hs + len;
    }
    return s->count > 1;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    int64_t available = pm_available(f);
    uint8_t *b, probe[32];
    bool result;
    if (available < 1 || available > 67108864 || stop(pd)) return false;
    if (available < 7 || !pm_read(f, 0, probe, 7) || xx_rt_memcmp(probe, "\x42\x4c\x45\x4e\x44\x45\x52", 7)) return false;
    b = (uint8_t *)xx_mem_alloc((size_t)available);
    if (!b) return false;
    result = pm_read(f, 0, b, (size_t)available) && parse_data(f, s, b, (uint64_t)available, pd);
    xx_mem_free(b);
    return result;
}

void xx_blender_blend_init(xx_blender_blend *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_BLENDER_BLEND, "blend");
    }
}
xx_blender_blend *xx_blender_blend_create(xx_io_device *d, int64_t b)
{
    xx_blender_blend *r = (xx_blender_blend *)xx_mem_alloc(sizeof(*r));
    if (r) xx_blender_blend_init(r, d, b);
    return r;
}
void xx_blender_blend_destroy(xx_blender_blend *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_blender_blend_free(xx_blender_blend *r)
{
    if (r) {
        xx_blender_blend_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_blender_blend_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_blender_blend_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
