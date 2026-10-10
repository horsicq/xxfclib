/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for Lyme self-extracting installer.
 */
#include "xxfclib/formats/lyme_sfx/xx_lyme_sfx.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#define LYME_SFX_MEMORY_CAP (UINT64_C(64) * 1024 * 1024)

typedef struct lyme_sfx_read_context {
    uint32_t crc;
    uint64_t resident;
} lyme_sfx_read_context;
typedef struct lyme_sfx_sink {
    uint8_t *p;
    size_t n, at;
} lyme_sfx_sink;
static uint32_t lyme_sfx_u32(const uint8_t *p)
{
    return xx_data_get_u32(p, 4, 0, false);
}
static bool lyme_sfx_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static uint64_t lyme_sfx_limit(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    return v ? xx_var_get_u64(v) : UINT64_C(256) * 1024 * 1024;
}
static uint64_t lyme_sfx_resident(pm_stream *s)
{
    uint64_t used = s ? (uint64_t)s->capacity * sizeof(pm_member) : 0;
    size_t i;
    if (s)
        for (i = 0; i < s->count; ++i) {
            pm_member *m = &s->items[i];
            uint64_t z = m->memory ? (uint64_t)m->size : 0;
            if (m->display_name) z += xx_rt_strlen(m->display_name) + 1;
            if (m->context) z += sizeof(lyme_sfx_read_context);
            used += z;
        }
    return used;
}
static bool lyme_sfx_budget(Abstractformat *f, pm_stream *s, uint64_t extra)
{
    uint64_t used = lyme_sfx_resident(s), limit = lyme_sfx_limit(f);
    return used <= limit && extra <= limit - used;
}
static bool lyme_sfx_decode_budget(Abstractformat *f, pm_member *m)
{
    lyme_sfx_read_context *c = m->context;
    return c && lyme_sfx_budget(f, NULL, c->resident + (uint64_t)m->packed_size + (uint64_t)m->size);
}
static bool lyme_sfx_device_leaf(const char *p, size_t n)
{
    char leaf[5];
    size_t i = 0;
    while (i < n && p[i] != '.' && i < sizeof(leaf) - 1) {
        unsigned char c = (unsigned char)p[i];
        leaf[i] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c);
        ++i;
    }
    leaf[i] = 0;
    if (i < n && p[i] != '.') return false;
    return !xx_rt_strcmp(leaf, "CON") || !xx_rt_strcmp(leaf, "PRN") || !xx_rt_strcmp(leaf, "AUX") || !xx_rt_strcmp(leaf, "NUL") ||
           (i == 4 && (!xx_rt_memcmp(leaf, "COM", 3) || !xx_rt_memcmp(leaf, "LPT", 3)) && leaf[3] >= '1' && leaf[3] <= '9');
}
static bool lyme_sfx_name(Abstractformat *f, pm_stream *s, const char *name, int64_t at, int64_t size)
{
    char *copy;
    size_t i, n = xx_rt_strlen(name), part = 0;
    if (!n || n > 4096 || name[0] == '/' || name[0] == '\\') return false;
    if (!lyme_sfx_budget(f, s, n + 1 + (s->count == s->capacity ? (uint64_t)(s->capacity ? s->capacity : 8) * sizeof(pm_member) : 0))) return false;
    copy = xx_str_dup(name);
    if (!copy) return false;
    for (i = 0; i <= n; ++i) {
        unsigned c = (unsigned char)copy[i];
        if (c == '\\') copy[i] = '/';
        if (c && (c < 32 || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')) goto bad;
        if (!c || copy[i] == '/') {
            size_t z = i - part;
            if (!z || (z == 1 && copy[part] == '.') || (z == 2 && copy[part] == '.' && copy[part + 1] == '.') || copy[i - 1] == '.' || copy[i - 1] == ' ' ||
                lyme_sfx_device_leaf(copy + part, z))
                goto bad;
            part = i + 1;
        }
    }
    if (!lyme_sfx_budget(f, s, s->count == s->capacity ? (uint64_t)(s->capacity ? s->capacity : 8) * sizeof(pm_member) : 0) || !pm_add(f, s, name, at, size)) goto bad;
    s->items[s->count - 1].display_name = copy;
    return true;
bad:
    xx_str_free(copy);
    return false;
}
static ssize_t lyme_sfx_write(xx_io_device *d, const void *p, size_t n)
{
    lyme_sfx_sink *s = d->priv;
    if (n > s->n - s->at) return -1;
    if (n) xx_rt_memcpy(s->p + s->at, p, n);
    s->at += n;
    return (ssize_t)n;
}
static bool lyme_sfx_inflate(const uint8_t *p, size_t n, uint8_t *out, size_t z, xx_pd_struct *pd)
{
    xx_io_device sink;
    lyme_sfx_sink s = {out, z, 0};
    size_t used = 0;
    xx_mem_zero(&sink, sizeof(sink));
    sink.priv = &s;
    sink.write = lyme_sfx_write;
    return !lyme_sfx_stop(pd) && xx_deflate_unpack_memory_to_device_ex(p, n, &sink, &used, false, pd) && used == n && s.at == z && !lyme_sfx_stop(pd);
}
static bool lyme_sfx_zlib_read(Abstractformat *f, pm_member *m, xx_io_device *out, xx_pd_struct *pd)
{
    uint8_t *packed = NULL, *plain = NULL;
    size_t written = (size_t)m->size, at = 0;
    bool ok = false;
    if (m->packed_size < 6 || (uint64_t)m->packed_size > LYME_SFX_MEMORY_CAP || (uint64_t)m->size > LYME_SFX_MEMORY_CAP || !lyme_sfx_decode_budget(f, m) ||
        lyme_sfx_stop(pd))
        return false;
    packed = xx_mem_alloc((size_t)m->packed_size);
    plain = xx_mem_alloc(m->size ? (size_t)m->size : 1);
    if (!packed || !plain || !pm_read(f, m->offset - f->base_address, packed, (size_t)m->packed_size)) goto done;
    if ((packed[0] & 15) != 8 || (packed[0] >> 4) > 7 || packed[1] & 32 || (((unsigned)packed[0] << 8) | packed[1]) % 31 ||
        !lyme_sfx_inflate(packed + 2, (size_t)m->packed_size - 6, plain, written, pd) || !xx_zlib_stream_trailer_matches(packed, (size_t)m->packed_size, plain, written))
        goto done;
    while (out && at < written) {
        size_t z = written - at;
        ssize_t got;
        if (z > 65536) z = 65536;
        if (lyme_sfx_stop(pd)) goto done;
        got = xx_io_write(out, plain + at, z);
        if (got <= 0 || (size_t)got > z) goto done;
        at += (size_t)got;
    }
    ok = !lyme_sfx_stop(pd);
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return ok;
}
static bool lyme_sfx_lyme_table(Abstractformat *f, pm_stream *s, int64_t at, uint32_t count, xx_pd_struct *pd)
{
    uint8_t h[12];
    uint32_t i;
    if (!count || count > 65536) return false;
    for (i = 0; i < count; ++i) {
        char name[4097];
        uint32_t len, offset, plain, packed;
        at -= 4;
        if (lyme_sfx_stop(pd) || !pm_read(f, at, h, 4)) return false;
        len = lyme_sfx_u32(h);
        if (!len || len > 4096) return false;
        at -= len;
        if (!pm_read(f, at, name, len) || xx_rt_memchr(name, 0, len)) return false;
        name[len] = 0;
        at -= 12;
        if (!pm_read(f, at, h, 12)) return false;
        offset = lyme_sfx_u32(h);
        plain = lyme_sfx_u32(h + 4);
        packed = lyme_sfx_u32(h + 8);
        if (!packed || plain > LYME_SFX_MEMORY_CAP || packed > LYME_SFX_MEMORY_CAP || (uint64_t)offset + packed > (uint64_t)at ||
            !lyme_sfx_name(f, s, name, offset, packed))
            return false;
        s->items[s->count - 1].size = plain;
        s->items[s->count - 1].compression_method = 8;
        s->items[s->count - 1].read_all = lyme_sfx_zlib_read;
    }
    s->size = pm_available(f);
    return true;
}
static bool lyme_sfx_lyme(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[18];
    int64_t n = pm_available(f), at = n - 18;
    uint32_t count;
    if (!pm_read(f, at, h, 18) || xx_rt_memcmp(h + 4, "1.10!LYME_SFX!", 14)) return false;
    count = lyme_sfx_u32(h);
    /* Optional application label preceding the count: zero, string, length,
     * then label kind (1 or 3). It is not another file-table record. */
    if (at >= 12 && pm_read(f, at - 8, h, 8) && (lyme_sfx_u32(h + 4) == 1 || lyme_sfx_u32(h + 4) == 3)) {
        uint32_t len = lyme_sfx_u32(h);
        int64_t begin = at - 8 - (int64_t)len - 4;
        if (len <= 4096 && begin >= 0 && pm_read(f, begin, h, 4) && !lyme_sfx_u32(h)) at = begin;
    }
    return lyme_sfx_lyme_table(f, s, at, count, pd);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    bool ok;
    size_t i;
    uint64_t resident;
    if (lyme_sfx_stop(pd) || !lyme_sfx_budget(f, s, 8 * sizeof(pm_member))) return false;
    ok = lyme_sfx_lyme(f, s, pd);
    if (!ok) return false;
    for (i = 0; i < s->count; ++i)
        if (s->items[i].read_all && !s->items[i].context) {
            lyme_sfx_read_context *c;
            if (!lyme_sfx_budget(f, s, sizeof(*c))) return false;
            c = xx_mem_calloc(1, sizeof(*c));
            if (!c) return false;
            s->items[i].context = c;
            s->items[i].free_context = xx_mem_free;
        }
    resident = lyme_sfx_resident(s);
    if (!lyme_sfx_budget(f, s, 0)) return false;
    for (i = 0; i < s->count; ++i)
        if (s->items[i].read_all) ((lyme_sfx_read_context *)s->items[i].context)->resident = resident;
    return true;
}

Abstractformat *xx_lyme_sfx_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) {
        xx_mem_zero(f, sizeof(*f));
        pm_init(f, d, base, XX_FILE_TYPE_LYME_SFX, "scr");
    }
    return f;
}
void xx_lyme_sfx_free(Abstractformat *f)
{
    if (f) {
        xx_format_cleanup_extra_parameters(f);
        xx_mem_free(f);
    }
}

xx_file_type_t xx_lyme_sfx_detect(xx_io_device *d, int64_t base)
{
    Abstractformat f;
    uint8_t h[40];
    int64_t cursor, n;
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    if (!d || base < 0 || xx_io_size(d) < base) return type;
    cursor = xx_io_tell(d);
    if (cursor < 0) return type;
    xx_mem_zero(&f, sizeof(f));
    f.device = d;
    f.base_address = base;
    n = pm_available(&f);
    if (n >= 24 && pm_read(&f, 0, h, 24) && pm_read(&f, n - 14, h, 14) && !xx_rt_memcmp(h, "1.10!LYME_SFX!", 14)) type = XX_FILE_TYPE_LYME_SFX;
    if (xx_io_seek64(d, cursor, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_lyme_sfx_open(xx_io_device *d)
{
    return xx_lyme_sfx_create(d, 0);
}
static const xx_file_type_t xx_lyme_sfx_types[] = {XX_FILE_TYPE_LYME_SFX};
static const xx_format_search_desc xx_lyme_sfx_desc = {xx_lyme_sfx_types, 1, NULL, 0, xx_lyme_sfx_open, xx_lyme_sfx_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(lyme_sfx, xx_lyme_sfx_desc)
