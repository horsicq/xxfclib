/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for VisualWare installer.
 */
#include "xxfclib/formats/visualware/xx_visualware.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#define VISUALWARE_MEMORY_CAP (UINT64_C(64) * 1024 * 1024)

typedef struct visualware_read_context {
    uint32_t crc;
    uint64_t resident;
} visualware_read_context;
typedef struct visualware_sink {
    uint8_t *p;
    size_t n, at;
} visualware_sink;
static uint32_t visualware_u32(const uint8_t *p)
{
    return xx_data_get_u32(p, 4, 0, false);
}
static bool visualware_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static bool visualware_span(uint64_t p, uint64_t z, uint64_t n)
{
    return p <= n && z <= n - p;
}
static uint64_t visualware_limit(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    return v ? xx_var_get_u64(v) : UINT64_C(256) * 1024 * 1024;
}
static uint64_t visualware_resident(pm_stream *s)
{
    uint64_t used = s ? (uint64_t)s->capacity * sizeof(pm_member) : 0;
    size_t i;
    if (s)
        for (i = 0; i < s->count; ++i) {
            pm_member *m = &s->items[i];
            uint64_t z = m->memory ? (uint64_t)m->size : 0;
            if (m->display_name) z += xx_rt_strlen(m->display_name) + 1;
            if (m->context) z += sizeof(visualware_read_context);
            used += z;
        }
    return used;
}
static bool visualware_budget(Abstractformat *f, pm_stream *s, uint64_t extra)
{
    uint64_t used = visualware_resident(s), limit = visualware_limit(f);
    return used <= limit && extra <= limit - used;
}
static bool visualware_device_leaf(const char *p, size_t n)
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
static bool visualware_name(Abstractformat *f, pm_stream *s, const char *name, int64_t at, int64_t size)
{
    char *copy;
    size_t i, n = xx_rt_strlen(name), part = 0;
    if (!n || n > 4096 || name[0] == '/' || name[0] == '\\') return false;
    if (!visualware_budget(f, s, n + 1 + (s->count == s->capacity ? (uint64_t)(s->capacity ? s->capacity : 8) * sizeof(pm_member) : 0))) return false;
    copy = xx_str_dup(name);
    if (!copy) return false;
    for (i = 0; i <= n; ++i) {
        unsigned c = (unsigned char)copy[i];
        if (c == '\\') copy[i] = '/';
        if (c && (c < 32 || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')) goto bad;
        if (!c || copy[i] == '/') {
            size_t z = i - part;
            if (!z || (z == 1 && copy[part] == '.') || (z == 2 && copy[part] == '.' && copy[part + 1] == '.') || copy[i - 1] == '.' || copy[i - 1] == ' ' ||
                visualware_device_leaf(copy + part, z))
                goto bad;
            part = i + 1;
        }
    }
    if (!visualware_budget(f, s, s->count == s->capacity ? (uint64_t)(s->capacity ? s->capacity : 8) * sizeof(pm_member) : 0) || !pm_add(f, s, name, at, size)) goto bad;
    s->items[s->count - 1].display_name = copy;
    return true;
bad:
    xx_str_free(copy);
    return false;
}
static bool visualware_memory(Abstractformat *f, pm_stream *s, const char *name, const uint8_t *p, size_t n)
{
    uint8_t *copy;
    if (n > VISUALWARE_MEMORY_CAP || !visualware_budget(f, s, n) || !visualware_name(f, s, name, 0, 0)) return false;
    if (!visualware_budget(f, s, n ? n : 1)) return false;
    copy = xx_mem_alloc(n ? n : 1);
    if (!copy) return false;
    if (n) xx_rt_memcpy(copy, p, n);
    s->items[s->count - 1].memory = copy;
    s->items[s->count - 1].size = (int64_t)n;
    return true;
}
static int64_t visualware_overlay(Abstractformat *f)
{
    uint8_t h[64], p[24], section[40];
    uint32_t pe;
    unsigned count, opt, i;
    uint64_t end, n = (uint64_t)pm_available(f);
    if (n < 2 || !pm_read(f, 0, h, 2)) return -1;
    if (h[0] != 'M' || h[1] != 'Z') return 0;
    if (n < 64 || !pm_read(f, 0, h, 64)) return -1;
    pe = visualware_u32(h + 60);
    if (!visualware_span(pe, 24, n) || !pm_read(f, pe, p, 24) || xx_rt_memcmp(p, "PE\0\0", 4)) return -1;
    count = xx_data_get_u16(p + 6, 2, 0, false);
    opt = xx_data_get_u16(p + 20, 2, 0, false);
    if (!count || count > 96 || opt < 64 || opt > 4096 || !visualware_span((uint64_t)pe + 24, opt + (uint64_t)count * 40, n) || !pm_read(f, (int64_t)pe + 84, h, 4))
        return -1;
    end = visualware_u32(h);
    for (i = 0; i < count; ++i) {
        uint64_t finish;
        if (!pm_read(f, (int64_t)pe + 24 + opt + (int64_t)i * 40, section, 40)) return -1;
        finish = (uint64_t)visualware_u32(section + 16) + visualware_u32(section + 20);
        if (finish > n) return -1;
        if (finish > end) end = finish;
    }
    /* Some signed launchers append their archive after WIN_CERTIFICATE. */
    if (!pm_read(f, (int64_t)pe + 24, h, 2)) return -1;
    {
        unsigned dd = xx_data_get_u16(h, 2, 0, false) == 0x10b ? 128 : xx_data_get_u16(h, 2, 0, false) == 0x20b ? 144 : 0;
        if (dd && opt >= dd + 8 && pm_read(f, (int64_t)pe + 24 + dd, h, 8)) {
            uint64_t cert = visualware_u32(h), bytes = visualware_u32(h + 4);
            if (bytes >= 8 && visualware_span(cert, bytes, n) && (cert == end || (visualware_span(end, bytes, n) && cert + bytes == n))) {
                uint64_t at = cert;
                bool prefix = cert != end;
                if (prefix) {
                    uint8_t first[8], last[8];
                    if (!pm_read(f, (int64_t)end, first, 8) || !pm_read(f, (int64_t)cert, last, 8) || xx_rt_memcmp(first, last, 8)) return (int64_t)end;
                    /* A re-signed launcher retains its older certificate
                     * before the appended archive. Their signatures differ. */
                }
                while (at < cert + bytes) {
                    uint32_t size;
                    if (!pm_read(f, (int64_t)at, h, 8)) return -1;
                    size = visualware_u32(h);
                    if (size < 8 || !visualware_span(at, size, cert + bytes) || xx_data_get_u16(h + 4, 2, 0, false) != 0x200 || xx_data_get_u16(h + 6, 2, 0, false) != 2)
                        return -1;
                    at = (at + size + 7) & ~UINT64_C(7);
                }
                if (at != cert + bytes) return -1;
                end = prefix ? end + bytes : at;
            }
        }
    }
    return end <= n ? (int64_t)end : -1;
}
static ssize_t visualware_write(xx_io_device *d, const void *p, size_t n)
{
    visualware_sink *s = d->priv;
    if (n > s->n - s->at) return -1;
    if (n) xx_rt_memcpy(s->p + s->at, p, n);
    s->at += n;
    return (ssize_t)n;
}
static bool visualware_inflate(const uint8_t *p, size_t n, uint8_t *out, size_t z, xx_pd_struct *pd)
{
    xx_io_device sink;
    visualware_sink s = {out, z, 0};
    size_t used = 0;
    xx_mem_zero(&sink, sizeof(sink));
    sink.priv = &s;
    sink.write = visualware_write;
    return !visualware_stop(pd) && xx_deflate_unpack_memory_to_device_ex(p, n, &sink, &used, false, pd) && used == n && s.at == z && !visualware_stop(pd);
}
static bool visualware_gzip(const uint8_t *p, size_t n, uint8_t *out, size_t z, xx_pd_struct *pd)
{
    return n >= 18 && !xx_rt_memcmp(p, "\x1f\x8b\x08\x00", 4) && visualware_u32(p + n - 4) == (uint32_t)z && visualware_inflate(p + 10, n - 18, out, z, pd) &&
           xx_crc32_calc(0, out, z) == visualware_u32(p + n - 8);
}
static bool visualware_visualware(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[28], *packed = NULL, *plain = NULL;
    uint64_t at, n = (uint64_t)pm_available(f);
    uint32_t z = 0, count = 0, block;
    unsigned i;
    size_t p = 0;
    bool ok = false, legacy = false;
    int64_t overlay = visualware_overlay(f);
    if (overlay < 0) return false;
    at = (uint64_t)overlay;
    for (i = 0; i < 3; ++i) {
        const char *sig = i == 0 ? "\\]_B" : i == 1 ? "\\]_V" : "\\]_Z";
        if (visualware_stop(pd) || !pm_read(f, (int64_t)at, h, 28) || xx_rt_memcmp(h, "JKMNPQSTVWYZ", 12) || xx_rt_memcmp(h + 12, sig, 4)) return false;
        if (!i) legacy = !xx_rt_memcmp(h + 20, "\xca\xfe\xba\xbe", 4);
        block = visualware_u32(h + (legacy ? 16 : 24));
        if (legacy && i == 2) {
            if (block < 4) return false;
            block -= 4;
            z = visualware_u32(h + 20);
        }
        at += legacy ? (i == 2 ? 24 : 20) : 28;
        if (!block || !visualware_span(at, block, n)) return false;
        if (i < 2) at = (at + block + 15) & ~UINT64_C(15);
    }
    if (block > VISUALWARE_MEMORY_CAP || block < 26 || !visualware_budget(f, s, block)) return false;
    packed = xx_mem_alloc(block);
    if (!packed || !pm_read(f, (int64_t)at, packed, block)) goto done;
    if (!legacy) {
        for (i = 0; i < block; ++i) {
            if (!(i & 65535U) && visualware_stop(pd)) goto done;
            packed[i] = (uint8_t)((packed[i] ^ (uint8_t)i) + 0x9c);
        }
        count = visualware_u32(packed);
        z = visualware_u32(packed + 4);
        if (!count || count > 65536) goto done;
    }
    if (z > VISUALWARE_MEMORY_CAP || !visualware_budget(f, s, (uint64_t)block + 2U * z + 2U * (uint64_t)(legacy ? 65536 : count) * sizeof(pm_member))) goto done;
    plain = xx_mem_alloc(z ? z : 1);
    if (!plain || !visualware_gzip(packed + (legacy ? 0 : 8), block - (legacy ? 0 : 8), plain, z, pd)) goto done;
    for (i = 0; legacy ? p < z : i < count; ++i) {
        char name[256];
        uint32_t size;
        size_t len;
        if (i >= 65536) goto done;
        if (visualware_stop(pd) || !visualware_span(p, 5, z) || visualware_u32(plain + p) != UINT32_C(0xeadc12f0)) goto done;
        len = plain[p + 4];
        p += 5;
        if (!len || !visualware_span(p, len + 5, z) || plain[p + len] != 0 || xx_rt_memchr(plain + p, 0, len)) goto done;
        xx_rt_memcpy(name, plain + p, len);
        name[len] = 0;
        p += len + 1;
        size = visualware_u32(plain + p);
        p += 4;
        if (!visualware_span(p, size, z) || !visualware_memory(f, s, name, plain + p, size)) goto done;
        s->items[s->count - 1].packed_size = block;
        s->items[s->count - 1].compression_method = 8;
        p += size;
    }
    if (p != z) goto done;
    s->size = (int64_t)n;
    ok = true;
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    return ok;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    bool ok;
    size_t i;
    uint64_t resident;
    if (visualware_stop(pd) || !visualware_budget(f, s, 8 * sizeof(pm_member))) return false;
    ok = visualware_visualware(f, s, pd);
    if (!ok) return false;
    for (i = 0; i < s->count; ++i)
        if (s->items[i].read_all && !s->items[i].context) {
            visualware_read_context *c;
            if (!visualware_budget(f, s, sizeof(*c))) return false;
            c = xx_mem_calloc(1, sizeof(*c));
            if (!c) return false;
            s->items[i].context = c;
            s->items[i].free_context = xx_mem_free;
        }
    resident = visualware_resident(s);
    if (!visualware_budget(f, s, 0)) return false;
    for (i = 0; i < s->count; ++i)
        if (s->items[i].read_all) ((visualware_read_context *)s->items[i].context)->resident = resident;
    return true;
}

Abstractformat *xx_visualware_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) {
        xx_mem_zero(f, sizeof(*f));
        pm_init(f, d, base, XX_FILE_TYPE_VISUALWARE, "exe");
    }
    return f;
}
void xx_visualware_free(Abstractformat *f)
{
    if (f) {
        xx_format_cleanup_extra_parameters(f);
        xx_mem_free(f);
    }
}

xx_file_type_t xx_visualware_detect(xx_io_device *d, int64_t base)
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
    if (n >= 24 && pm_read(&f, 0, h, 24)) {
        int64_t at = visualware_overlay(&f);
        if (at >= 0 && pm_read(&f, at, h, 19) && !xx_rt_memcmp(h, "JKMNPQSTVWYZ\\]_B", 16)) type = XX_FILE_TYPE_VISUALWARE;
    }
    if (xx_io_seek64(d, cursor, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_visualware_open(xx_io_device *d)
{
    return xx_visualware_create(d, 0);
}
static const xx_file_type_t xx_visualware_types[] = {XX_FILE_TYPE_VISUALWARE};
static const xx_format_search_desc xx_visualware_desc = {xx_visualware_types, 1, NULL, 0, xx_visualware_open, xx_visualware_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(visualware, xx_visualware_desc)
