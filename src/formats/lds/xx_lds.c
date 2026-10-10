/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for LDS compressed archive.
 */
#include "xxfclib/formats/lds/xx_lds.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#define LDS_MEMORY_CAP (256U * 1024U * 1024U)

static uint32_t lds_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t lds_u64(const uint8_t *p)
{
    return lds_u32(p) | ((uint64_t)lds_u32(p + 4) << 32);
}
static uint64_t lds_budget(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n = v ? xx_var_get_u64(v) : LDS_MEMORY_CAP;
    return n < LDS_MEMORY_CAP ? n : LDS_MEMORY_CAP;
}
static bool lds_take(Abstractformat *f, int64_t *at, uint64_t n, void *p)
{
    if (*at < 0 || (uint64_t)*at > (uint64_t)pm_available(f) || n > (uint64_t)(pm_available(f) - *at)) return false;
    if (p && !pm_read(f, *at, p, (size_t)n)) return false;
    *at += (int64_t)n;
    return true;
}
static bool lds_word(Abstractformat *f, int64_t *at, uint32_t *v)
{
    uint8_t h[4];
    if (!lds_take(f, at, 4, h)) return false;
    *v = lds_u32(h);
    return true;
}
static bool lds_room(Abstractformat *f, pm_stream *s, uint64_t extra, bool adding)
{
    size_t i, capacity = s->capacity;
    uint64_t used = extra, budget = lds_budget(f);
    if (adding && s->count == capacity) capacity = capacity ? capacity * 2U : 8U;
    if (used > budget || (uint64_t)capacity * sizeof(pm_member) > budget - used) return false;
    used += (uint64_t)capacity * sizeof(pm_member);
    for (i = 0; i < s->count; ++i) {
        uint64_t n = s->items[i].memory ? (uint64_t)s->items[i].size : 0;
        if (s->items[i].password) n += xx_rt_strlen(s->items[i].password) + 1U;
        if (n > budget - used) return false;
        used += n;
    }
    return true;
}
static bool lds_add(Abstractformat *f, pm_stream *s, const char *name, int64_t at, int64_t n)
{
    return lds_room(f, s, 0, true) && pm_add(f, s, name, at, n);
}
static bool lds_lds(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[20];
    int64_t at;
    uint32_t count, i;
    if (!pm_read(f, 0, h, 14) || xx_mem_compare(h, "STOR\1\1", 6) || lds_u64(h + 6) < 14U || lds_u64(h + 6) > (uint64_t)pm_available(f)) return false;
    at = (int64_t)lds_u64(h + 6);
    if (!lds_word(f, &at, &count) || count > 1000000U) return false;
    for (i = 0; i < count; ++i) {
        uint64_t offset, size;
        uint32_t n;
        uint8_t raw[1024];
        char name[96];
        size_t j, copy;
        if ((pd && xx_pd_is_stopped(pd)) || !lds_take(f, &at, 20, h)) return false;
        offset = lds_u64(h);
        size = lds_u64(h + 8);
        n = lds_u32(h + 16);
        if (n > 1024U || !lds_take(f, &at, n, raw) || offset > INT64_MAX || size > INT64_MAX) return false;
        copy = n < 95U ? n : 95U;
        for (j = 0; j < copy; ++j) name[j] = raw[j] >= 32 && raw[j] < 127 ? (char)raw[j] : '_';
        name[copy] = 0;
        if (!lds_add(f, s, name, (int64_t)offset, (int64_t)size)) return false;
    }
    s->size = pm_available(f);
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    if (pd && xx_pd_is_stopped(pd)) return false;
    return lds_lds(f, s, pd);
}

Abstractformat *xx_lds_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_LDS, "bin");
    return f;
}
void xx_lds_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}

xx_file_type_t xx_lds_detect(xx_io_device *d, int64_t base)
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
    if (n > 32) n = 32;
    if (!pm_read(&f, 0, h, (size_t)n)) goto done;
    if (n >= 14 && !xx_mem_compare(h, "STOR\1\1", 6)) type = XX_FILE_TYPE_LDS;
done:
    if (xx_io_seek64(d, cursor, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_lds_open(xx_io_device *d)
{
    return xx_lds_create(d, 0);
}
static const xx_file_type_t xx_lds_types[] = {XX_FILE_TYPE_LDS};
static const xx_format_search_desc xx_lds_desc = {xx_lds_types, 1, NULL, 0, xx_lds_open, xx_lds_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(lds, xx_lds_desc)
