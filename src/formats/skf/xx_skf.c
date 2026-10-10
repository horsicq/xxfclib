/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for SkinCrafter resources.
 */
#include "xxfclib/formats/skf/xx_skf.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#define SKF_MEMORY_CAP (256U * 1024U * 1024U)

static uint16_t skf_u16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}
static uint32_t skf_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t skf_budget(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n = v ? xx_var_get_u64(v) : SKF_MEMORY_CAP;
    return n < SKF_MEMORY_CAP ? n : SKF_MEMORY_CAP;
}
static bool skf_name(Abstractformat *f, int64_t *at, char *name, size_t cap, bool long_size)
{
    uint8_t h[4], raw[1024];
    uint32_t n;
    size_t i, copy;
    if (!pm_read(f, *at, h, long_size ? 4 : 1)) return false;
    *at += long_size ? 4 : 1;
    n = long_size ? skf_u32(h) : h[0];
    if (n > 1024U || !pm_read(f, *at, raw, n)) return false;
    *at += n;
    if (name && cap) {
        copy = n < cap - 1U ? n : cap - 1U;
        for (i = 0; i < copy; ++i) name[i] = raw[i] >= 32 && raw[i] < 127 ? (char)raw[i] : '_';
        name[copy] = 0;
    }
    return true;
}
static bool skf_take(Abstractformat *f, int64_t *at, uint64_t n, void *p)
{
    if (*at < 0 || (uint64_t)*at > (uint64_t)pm_available(f) || n > (uint64_t)(pm_available(f) - *at)) return false;
    if (p && !pm_read(f, *at, p, (size_t)n)) return false;
    *at += (int64_t)n;
    return true;
}
static bool skf_word(Abstractformat *f, int64_t *at, uint32_t *v)
{
    uint8_t h[4];
    if (!skf_take(f, at, 4, h)) return false;
    *v = skf_u32(h);
    return true;
}
static bool skf_room(Abstractformat *f, pm_stream *s, uint64_t extra, bool adding)
{
    size_t i, capacity = s->capacity;
    uint64_t used = extra, budget = skf_budget(f);
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
static bool skf_add(Abstractformat *f, pm_stream *s, const char *name, int64_t at, int64_t n)
{
    return skf_room(f, s, 0, true) && pm_add(f, s, name, at, n);
}
static bool skf_skf_group(Abstractformat *f, pm_stream *s, int64_t *at, unsigned version, const char *object, xx_pd_struct *pd)
{
    uint32_t count, i, n;
    uint8_t h[2];
    if (!skf_word(f, at, &count) || count > 1000000U) return false;
    for (i = 0; i < count; ++i) {
        char name[128];
        const char *ext = "bin";
        int64_t start;
        if ((pd && xx_pd_is_stopped(pd)) || (version > 218U && !skf_take(f, at, 4, NULL)) || !skf_word(f, at, &n) || n > INT32_MAX || !skf_take(f, at, 2, h))
            return false;
        start = *at;
        if (n > 2U) {
            if (!pm_read(f, start, h, 2)) return false;
            if (h[0] == 'B' && h[1] == 'M') ext = "bmp";
            else if (h[0] == 137U && h[1] == 'P') ext = "png";
        }
        xx_rt_snprintf(name, sizeof(name), "%s_%u.%s", object, (unsigned)s->count + 1U, ext);
        if (!skf_take(f, at, n, NULL) || !skf_add(f, s, name, start, n) || !skf_take(f, at, version > 205U ? 24U : 20U, NULL)) return false;
    }
    if (!skf_word(f, at, &n) || n > INT32_MAX / 6U || !skf_take(f, at, (uint64_t)n * 6U, NULL)) return false;
    if (version > 201U) {
        if (!skf_word(f, at, &n) || n > INT32_MAX / 6U || !skf_take(f, at, (uint64_t)n * 6U, NULL)) return false;
        if (version > 206U) {
            if (!skf_word(f, at, &n)) return false;
            if (n && (!skf_take(f, at, 27, NULL) || !skf_name(f, at, NULL, 0, false))) return false;
        }
        if (version > 218U && !skf_take(f, at, 4, NULL)) return false;
    }
    return skf_take(f, at, 4, NULL);
}
static bool skf_skf(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    char version_name[16];
    int64_t at = 0;
    unsigned version;
    uint32_t objects, i, j;
    if (!skf_name(f, &at, version_name, sizeof(version_name), false) || xx_rt_strlen(version_name) < 6U || xx_mem_compare(version_name, "SKF", 3) ||
        version_name[3] < '0' || version_name[3] > '9' || version_name[4] != '.' || version_name[5] < '0' || version_name[5] > '9')
        return false;
    version = (unsigned)(version_name[3] - '0') * 100U + (unsigned)(version_name[5] - '0');
    if (version_name[6]) {
        if (version_name[6] < '0' || version_name[6] > '9' || version_name[7]) return false;
        version = version / 100U * 100U + (unsigned)(version_name[5] - '0') * 10U + (unsigned)(version_name[6] - '0');
    }
    if (version < 200U || version > 299U) return false;
    for (i = 0; i < (version > 218U ? 10U : 4U); ++i)
        if (!skf_name(f, &at, NULL, 0, true)) return false;
    if (!skf_word(f, &at, &objects) || objects > 1000000U) return false;
    for (i = 0; i < objects; ++i) {
        char object[96];
        uint32_t properties;
        if ((pd && xx_pd_is_stopped(pd)) || !skf_name(f, &at, object, sizeof(object), false) || !object[0] || !skf_word(f, &at, &properties) || properties > 65535U)
            return false;
        for (j = 0; j < properties; ++j) {
            uint8_t h[2];
            unsigned prop, k;
            uint32_t n, m;
            if (!skf_take(f, &at, 2, h) || !(prop = skf_u16(h)) || !skf_skf_group(f, s, &at, version, object, pd)) return false;
            if (prop == 13U) {
                if (!skf_take(f, &at, 4, NULL)) return false;
            } else if (prop == 44U) {
                if (!skf_word(f, &at, &n) || n > INT32_MAX || !skf_take(f, &at, n, NULL)) return false;
            } else if (prop == 2U) {
                for (k = 0; k < 4U; ++k) {
                    if (!skf_skf_group(f, s, &at, version, object, pd) || !skf_word(f, &at, &n) || n > INT32_MAX / 14U || !skf_take(f, &at, (uint64_t)n * 14U, NULL) ||
                        !skf_word(f, &at, &m) || m > 1000000U)
                        return false;
                    while (m--) {
                        if (!skf_take(f, &at, 14, NULL) || !skf_word(f, &at, &n) || n > INT32_MAX || !skf_take(f, &at, n, NULL)) return false;
                    }
                    if (version < 209U && !skf_take(f, &at, 4, NULL)) return false;
                }
                if (!skf_skf_group(f, s, &at, version, object, pd)) return false;
                if (version <= 208U && !skf_take(f, &at, version == 200U ? 100U : version == 201U ? 148U : 76U, NULL)) return false;
            }
        }
    }
    /* SkinCrafter appends a version-specific global settings block. Preserve
     * it alongside the resource payloads rather than discarding unknown fields. */
    if (at < pm_available(f) && !skf_add(f, s, "skin-metadata.bin", at, pm_available(f) - at)) return false;
    s->size = pm_available(f);
    return true;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    if (pd && xx_pd_is_stopped(pd)) return false;
    return skf_skf(f, s, pd);
}

Abstractformat *xx_skf_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_SKF, "bin");
    return f;
}
void xx_skf_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}

xx_file_type_t xx_skf_detect(xx_io_device *d, int64_t base)
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
    if (n >= 7 && (h[0] == 6 || h[0] == 7) && !xx_mem_compare(h + 1, "SKF2.", 5)) type = XX_FILE_TYPE_SKF;
done:
    if (xx_io_seek64(d, cursor, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_skf_open(xx_io_device *d)
{
    return xx_skf_create(d, 0);
}
static const xx_file_type_t xx_skf_types[] = {XX_FILE_TYPE_SKF};
static const xx_format_search_desc xx_skf_desc = {xx_skf_types, 1, NULL, 0, xx_skf_open, xx_skf_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(skf, xx_skf_desc)
