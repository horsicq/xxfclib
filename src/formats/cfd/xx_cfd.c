/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for CopyFloppyDisk compressed image.
 */
#include "xxfclib/formats/cfd/xx_cfd.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/crc/xx_crc.h"
#include "xxfclib/algo/dcl/xx_dcl.h"
#define CFD_MEMORY_CAP (256U * 1024U * 1024U)

static uint16_t cfd_u16(const uint8_t *p)
{
    return (uint16_t)((unsigned)p[0] | ((unsigned)p[1] << 8));
}
static uint32_t cfd_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t cfd_budget(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n = v ? xx_var_get_u64(v) : CFD_MEMORY_CAP;
    return n < CFD_MEMORY_CAP ? n : CFD_MEMORY_CAP;
}
static bool cfd_take(Abstractformat *f, int64_t *at, uint64_t n, void *p)
{
    if (*at < 0 || (uint64_t)*at > (uint64_t)pm_available(f) || n > (uint64_t)(pm_available(f) - *at)) return false;
    if (p && !pm_read(f, *at, p, (size_t)n)) return false;
    *at += (int64_t)n;
    return true;
}
static bool cfd_room(Abstractformat *f, pm_stream *s, uint64_t extra, bool adding)
{
    size_t i, capacity = s->capacity;
    uint64_t used = extra, budget = cfd_budget(f);
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
static bool cfd_add(Abstractformat *f, pm_stream *s, const char *name, int64_t at, int64_t n)
{
    return cfd_room(f, s, 0, true) && pm_add(f, s, name, at, n);
}
static bool cfd_owned(Abstractformat *f, pm_stream *s, const char *name, uint8_t *data, size_t plain, int64_t offset, int64_t packed)
{
    if (!cfd_room(f, s, plain, true) || !cfd_add(f, s, name, offset, packed)) {
        xx_mem_free(data);
        return false;
    }
    s->items[s->count - 1U].memory = data;
    s->items[s->count - 1U].size = (int64_t)plain;
    return true;
}
static bool cfd_cfd(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[32], small[2], *image = NULL;
    int64_t at = 32, size = pm_available(f);
    size_t out = 0, expected;
    bool result = false;
    if (!pm_read(f, 0, h, 32) || cfd_u32(h) != 0 || cfd_u16(h + 4) != 0 || h[6] || cfd_u16(h + 7) != 512U || h[12] != 2U ||
        (cfd_u16(h + 22) != 1U && cfd_u16(h + 22) != 2U) || cfd_u16(h + 24) != 0U || (cfd_u16(h + 26) != 0U && cfd_u16(h + 26) != 0x800U))
        return false;
    if (cfd_u16(h + 26) & 0x800U) {
        if (!cfd_add(f, s, "floppy.ima", 32, size - 32)) return false;
        s->size = size;
        return true;
    }
    expected = (size_t)cfd_u16(h + 15) * 512U;
    if (!expected || !cfd_room(f, s, (uint64_t)expected + 65536U, true)) return false;
    image = (uint8_t *)xx_mem_alloc(expected);
    if (!image) return false;
    while (at < size) {
        uint16_t packed, index_size;
        uint8_t *p;
        size_t n, table, position, data_left;
        if ((pd && xx_pd_is_stopped(pd)) || !cfd_take(f, &at, 2, small)) goto done;
        packed = cfd_u16(small);
        if (packed < 4U || packed - 2U > (uint64_t)(size - at)) goto done;
        n = packed - 2U;
        p = (uint8_t *)xx_mem_alloc(n);
        if (!p || !pm_read(f, at, p, n)) {
            xx_mem_free(p);
            goto done;
        }
        index_size = cfd_u16(p);
        if (index_size < 2U || (index_size - 2U) % 4U || index_size > n) {
            xx_mem_free(p);
            goto done;
        }
        position = index_size;
        data_left = n - index_size;
        for (table = 2; table < index_size; table += 4) {
            size_t last = cfd_u16(p + table), run = cfd_u16(p + table + 2), copy;
            if (last < position || (copy = last - position + 1U) > data_left || copy > expected - out || run > expected - out - copy) {
                xx_mem_free(p);
                goto done;
            }
            xx_mem_copy(image + out, p + position, copy);
            out += copy;
            position += copy;
            data_left -= copy;
            while (run--) {
                image[out] = image[out - 1U];
                ++out;
            }
        }
        if (data_left > expected - out) {
            xx_mem_free(p);
            goto done;
        }
        xx_mem_copy(image + out, p + position, data_left);
        out += data_left;
        at += (int64_t)n;
        xx_mem_free(p);
    }
    if (out != expected) goto done;
    result = cfd_owned(f, s, "floppy.ima", image, out, 32, size - 32);
    image = NULL;
    s->size = size;
done:
    xx_mem_free(image);
    return result;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    if (pd && xx_pd_is_stopped(pd)) return false;
    return cfd_cfd(f, s, pd);
}

Abstractformat *xx_cfd_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_CFD, "bin");
    return f;
}
void xx_cfd_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}

xx_file_type_t xx_cfd_detect(xx_io_device *d, int64_t base)
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
    if (n >= 32 && cfd_u32(h) == 0 && cfd_u16(h + 4) == 0 && !h[6] && cfd_u16(h + 7) == 512U && h[12] == 2 && (cfd_u16(h + 22) == 1 || cfd_u16(h + 22) == 2) &&
        cfd_u16(h + 24) == 0 && (cfd_u16(h + 26) == 0 || cfd_u16(h + 26) == 2048U))
        type = XX_FILE_TYPE_CFD;
done:
    if (xx_io_seek64(d, cursor, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_cfd_open(xx_io_device *d)
{
    return xx_cfd_create(d, 0);
}
static const xx_file_type_t xx_cfd_types[] = {XX_FILE_TYPE_CFD};
static const xx_format_search_desc xx_cfd_desc = {xx_cfd_types, 1, NULL, 0, xx_cfd_open, xx_cfd_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(cfd, xx_cfd_desc)
