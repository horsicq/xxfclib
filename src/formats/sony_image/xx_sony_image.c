/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded native reader for Sony TCJN image.
 */
#include "xxfclib/formats/sony_image/xx_sony_image.h"
#include "../xx_payload_members.h"
#include "xxfclib/algo/deflate/xx_deflate.h"
#include "xxfclib/algo/crc/xx_crc.h"
#define SONY_IMAGE_MEMORY_CAP (256U * 1024U * 1024U)

typedef struct sony_image_buffer {
    uint8_t *data;
    size_t size, capacity, limit;
} sony_image_buffer;
static uint32_t sony_image_u32(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t sony_image_budget(Abstractformat *f)
{
    const xx_var *v = xx_format_resolve_extra_parameter(f, NULL, XX_META_ID_OPT_MEMORY_LIMIT);
    uint64_t n = v ? xx_var_get_u64(v) : SONY_IMAGE_MEMORY_CAP;
    return n < SONY_IMAGE_MEMORY_CAP ? n : SONY_IMAGE_MEMORY_CAP;
}
static ssize_t sony_image_buffer_write(xx_io_device *d, const void *p, size_t n)
{
    sony_image_buffer *b = d->priv;
    if (n > b->limit - b->size) return -1;
    if (b->capacity - b->size < n) {
        size_t cap = b->capacity ? b->capacity : (b->limit < 65536U ? b->limit : 65536U);
        uint8_t *next;
        while (cap < b->size + n) {
            if (cap > b->limit / 2U) {
                cap = b->limit;
                break;
            }
            cap *= 2U;
        }
        next = xx_mem_realloc(b->data, cap);
        if (!next) return -1;
        b->data = next;
        b->capacity = cap;
    }
    if (n) xx_mem_copy(b->data + b->size, p, n);
    b->size += n;
    return (ssize_t)n;
}
static bool sony_image_sony_header(Abstractformat *f, uint8_t *mode)
{
    uint8_t h[26];
    int64_t n = pm_available(f);
    if (n < 512 || !pm_read(f, 0, h, sizeof(h)) || xx_mem_compare(h, "TCJN", 4) || sony_image_u32(h + 16) || sony_image_u32(h + 20) != (uint64_t)(n - 512) || h[25] > 1U)
        return false;
    *mode = h[24];
    return true;
}
static bool sony_image_sony(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t mode, h[6], tail[4];
    sony_image_buffer b = {0};
    xx_io_device sink;
    int64_t used = 0, size = pm_available(f);
    uint64_t reserve = 524288U + (uint64_t)sizeof(pm_member) * 8U, budget = sony_image_budget(f);
    bool result = false;
    uint32_t adler;
    if ((pd && xx_pd_is_stopped(pd)) || !sony_image_sony_header(f, &mode)) return false;
    if (mode) {
        if (pd) xx_pd_set_error(pd, -1, "Sony Image continuation requires the initial .IMG volume");
        return false;
    }
    if (size < 522 || !pm_read(f, 512, h, 6) || xx_mem_compare(h, "ZLIB", 4) || !xx_zlib_stream_header_is_valid(h + 4, 2) || budget <= reserve) return false;
    b.limit = (size_t)(budget - reserve);
    if (b.limit > 64U * 1024U * 1024U) b.limit = 64U * 1024U * 1024U;
    xx_mem_zero(&sink, sizeof(sink));
    sink.priv = &b;
    sink.write = sony_image_buffer_write;
    if (!xx_deflate_unpack_device_ex(f->device, f->base_address + 518, size - 518, &sink, false, 0, NULL, 0, &used, pd) || used != size - 522 ||
        !pm_read(f, 518 + used, tail, 4) || (pd && xx_pd_is_stopped(pd)))
        goto done;
    adler = ((uint32_t)tail[0] << 24) | ((uint32_t)tail[1] << 16) | ((uint32_t)tail[2] << 8) | tail[3];
    if (adler != xx_zlib_stream_adler32(b.data, b.size) || !pm_add(f, s, "image.img", 516, size - 516)) goto done;
    s->items[0].memory = b.data;
    b.data = NULL;
    s->items[0].size = (int64_t)b.size;
    s->items[0].compression_method = 8;
    s->size = size;
    result = true;
done:
    xx_mem_free(b.data);
    return result;
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    if (pd && xx_pd_is_stopped(pd)) return false;
    return sony_image_sony(f, s, pd);
}

Abstractformat *xx_sony_image_create(xx_io_device *d, int64_t base)
{
    Abstractformat *f = (Abstractformat *)xx_mem_alloc(sizeof(*f));
    if (f) pm_init(f, d, base, XX_FILE_TYPE_SONY_IMAGE, "img");
    return f;
}
void xx_sony_image_free(Abstractformat *f)
{
    if (f) {
        xx_format_destroy(f);
        xx_mem_free(f);
    }
}

xx_file_type_t xx_sony_image_detect(xx_io_device *d, int64_t base)
{
    Abstractformat f;
    int64_t cursor;
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    if (!d || base < 0 || xx_io_size(d) < base) return type;
    cursor = xx_io_tell(d);
    if (cursor < 0) return type;
    xx_mem_zero(&f, sizeof(f));
    f.device = d;
    f.base_address = base;
    {
        uint8_t mode;
        if (sony_image_sony_header(&f, &mode)) type = XX_FILE_TYPE_SONY_IMAGE;
    }
    if (xx_io_seek64(d, cursor, SEEK_SET)) return XX_FILE_TYPE_UNKNOWN;
    return type;
}

#include "../xx_format_abstract_extractor_adapter.h"
static Abstractformat *xx_sony_image_open(xx_io_device *d)
{
    return xx_sony_image_create(d, 0);
}
static const xx_file_type_t xx_sony_image_types[] = {XX_FILE_TYPE_SONY_IMAGE};
static const xx_format_search_desc xx_sony_image_desc = {xx_sony_image_types, 1, NULL, 0, xx_sony_image_open, xx_sony_image_free, true};
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(sony_image, xx_sony_image_desc)
