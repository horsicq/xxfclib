/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for EVD map images.
 */
#include "xxfclib/formats/evd/xx_evd.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/lzh/xx_lzh.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/data/xx_data.h"
#define EVD_MEMORY_CAP (UINT64_C(64) * 1024 * 1024)

typedef struct evd_read_context {
    uint32_t crc;
    uint64_t resident;
} evd_read_context;
static uint32_t evd_u32(const uint8_t *p)
{
    return xx_data_get_u32(p, 4, 0, false);
}
static bool evd_stop(xx_pd_struct *pd)
{
    return pd && xx_pd_is_stopped(pd);
}
static uint64_t evd_limit(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    return v ? xx_var_get_u64(v) : UINT64_C(256) * 1024 * 1024;
}
static uint64_t evd_resident(pm_stream *s)
{
    uint64_t used = s ? (uint64_t)s->capacity * sizeof(pm_member) : 0;
    size_t i;
    if (s)
        for (i = 0; i < s->count; ++i) {
            pm_member *m = &s->items[i];
            uint64_t z = m->memory ? (uint64_t)m->size : 0;
            if (m->display_name) z += xx_rt_strlen(m->display_name) + 1;
            if (m->context) z += sizeof(evd_read_context);
            used += z;
        }
    return used;
}
static bool evd_budget(Abstractformat *f, pm_stream *s, uint64_t extra)
{
    uint64_t used = evd_resident(s), limit = evd_limit(f);
    return used <= limit && extra <= limit - used;
}
static bool evd_device_leaf(const char *p, size_t n)
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
static bool evd_name(Abstractformat *f, pm_stream *s, const char *name, int64_t at, int64_t size)
{
    char *copy;
    size_t i, n = xx_rt_strlen(name), part = 0;
    if (!n || n > 4096 || name[0] == '/' || name[0] == '\\') return false;
    if (!evd_budget(f, s, n + 1 + (s->count == s->capacity ? (uint64_t)(s->capacity ? s->capacity : 8) * sizeof(pm_member) : 0))) return false;
    copy = xx_str_dup(name);
    if (!copy) return false;
    for (i = 0; i <= n; ++i) {
        unsigned c = (unsigned char)copy[i];
        if (c == '\\') copy[i] = '/';
        if (c && (c < 32 || c == ':' || c == '<' || c == '>' || c == '"' || c == '|' || c == '?' || c == '*')) goto bad;
        if (!c || copy[i] == '/') {
            size_t z = i - part;
            if (!z || (z == 1 && copy[part] == '.') || (z == 2 && copy[part] == '.' && copy[part + 1] == '.') || copy[i - 1] == '.' || copy[i - 1] == ' ' ||
                evd_device_leaf(copy + part, z))
                goto bad;
            part = i + 1;
        }
    }
    if (!evd_budget(f, s, s->count == s->capacity ? (uint64_t)(s->capacity ? s->capacity : 8) * sizeof(pm_member) : 0) || !pm_add(f, s, name, at, size)) goto bad;
    s->items[s->count - 1].display_name = copy;
    return true;
bad:
    xx_str_free(copy);
    return false;
}
static bool evd_evd(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[24], sig[8];
    int64_t at = 24, n = pm_available(f);
    if (!pm_read(f, 0, h, 24) || evd_u32(h) || evd_u32(h + 4) || evd_u32(h + 8) != 12 || (evd_u32(h + 12) != 1 && evd_u32(h + 12) != 2) || !evd_u32(h + 16) ||
        !evd_u32(h + 20))
        return false;
    while (at < n) {
        uint16_t type;
        uint32_t id, z;
        char name[48];
        if (evd_stop(pd) || !pm_read(f, at, h, 12)) return false;
        type = xx_data_get_u16(h, 2, 0, false);
        id = evd_u32(h + 4);
        z = evd_u32(h + 8);
        if (type != xx_data_get_u16(h + 2, 2, 0, false) || (type != 1 && type != 2) || !id || z < 8 || !pm_read(f, at + 12, sig, 8)) return false;
        if (type == 1 ? (sig[0] != 255 || sig[1] != 216 || sig[2] != 255) : xx_rt_memcmp(sig, "\x89PNG\r\n\x1a\n", 8)) return false;
        xx_rt_snprintf(name, sizeof(name), "%u.%s", id, type == 1 ? "jpg" : "png");
        if (!evd_name(f, s, name, at + 12, z)) return false;
        at += 12 + (int64_t)z;
    }
    s->size = n;
    return s->count && at == n;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    bool ok;
    size_t i;
    uint64_t resident;
    if (evd_stop(pd) || !evd_budget(f, s, 8 * sizeof(pm_member))) return false;
    ok = evd_evd(f, s, pd);
    if (!ok) return false;
    for (i = 0; i < s->count; ++i)
        if (s->items[i].read_all && !s->items[i].context) {
            evd_read_context *c;
            if (!evd_budget(f, s, sizeof(*c))) return false;
            c = xx_mem_calloc(1, sizeof(*c));
            if (!c) return false;
            s->items[i].context = c;
            s->items[i].free_context = xx_mem_free;
        }
    resident = evd_resident(s);
    if (!evd_budget(f, s, 0)) return false;
    for (i = 0; i < s->count; ++i)
        if (s->items[i].read_all) ((evd_read_context *)s->items[i].context)->resident = resident;
    return true;
}

Abstractformat *xx_evd_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) {
        xx_mem_zero(f, sizeof(*f));
        pm_init(f, d, base, XX_FILE_TYPE_EVD, "evd");
    }
    return f;
}
void xx_evd_free(Abstractformat *f)
{
    if (f) {
        xx_format_cleanup_extra_parameters(f);
        xx_mem_free(f);
    }
}

xx_file_type_t xx_evd_detect(xx_io_device *d, int64_t base)
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
    if (n >= 24 && pm_read(&f, 0, h, 24) && !evd_u32(h) && !evd_u32(h + 4) && evd_u32(h + 8) == 12 && (evd_u32(h + 12) == 1 || evd_u32(h + 12) == 2) && evd_u32(h + 16) &&
        evd_u32(h + 20))
        type = XX_FILE_TYPE_EVD;
    if (xx_io_seek64(d, cursor, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_evd_open(xx_io_device *d)
{
    return xx_evd_create(d, 0);
}
static const xx_file_type_t xx_evd_types[] = {XX_FILE_TYPE_EVD};
static const xx_format_search_desc xx_evd_desc = {xx_evd_types, 1, NULL, 0, xx_evd_open, xx_evd_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(evd, xx_evd_desc)
