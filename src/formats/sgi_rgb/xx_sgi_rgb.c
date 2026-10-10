/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://ftp.zx.net.nz/pub/archive/ftp.sgi.com/graphics/grafica/sgiimage.html
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/sgi_rgb/xx_sgi_rgb.h"
#include "../xx_payload_members.h"
#include "xxfclib/global/xx_global.h"
#include "xxfclib/data/xx_data.h"

typedef struct fm_bytes {
    Abstractformat *f;
    xx_pd_struct *pd;
    int64_t pos, end, begin;
    size_t count, capacity;
    uint8_t *buffer;
} fm_bytes;
static bool fm_start(fm_bytes *r, Abstractformat *f, xx_pd_struct *pd, int64_t at, int64_t end)
{
    xx_mem_zero(r, sizeof(*r));
    r->f = f;
    r->pd = pd;
    r->pos = at;
    r->end = end;
    r->begin = -1;
    r->capacity = xx_get_file_buffer_size();
    if (r->capacity > (SIZE_MAX >> 1)) r->capacity = SIZE_MAX >> 1;
    if (end < at) return false;
    if ((uint64_t)(end - at) < r->capacity) r->capacity = (size_t)(end - at);
    if (!r->capacity) r->capacity = 1;
    r->buffer = (uint8_t *)xx_mem_alloc(r->capacity);
    return r->buffer != NULL;
}
static bool fm_finish(fm_bytes *r, bool result)
{
    xx_mem_free(r->buffer);
    return result;
}
static bool fm_byte(fm_bytes *r, uint8_t *b)
{
    if (r->pos >= r->end) return false;
    if (r->begin < 0 || r->pos < r->begin || r->pos - r->begin >= (int64_t)r->count) {
        int64_t left = r->end - r->pos;
        r->count = (uint64_t)left > r->capacity ? r->capacity : (size_t)left;
        if ((r->pd && xx_pd_is_stopped(r->pd)) || !pm_read(r->f, r->pos, r->buffer, r->count)) {
            return false;
        }
        r->begin = r->pos;
    }
    *b = r->buffer[(size_t)(r->pos - r->begin)];
    ++r->pos;
    return true;
}
static bool fm_skip(fm_bytes *r, uint64_t n)
{
    if (r->pos > r->end || n > (uint64_t)(r->end - r->pos) || (r->pd && xx_pd_is_stopped(r->pd))) return false;
    r->pos += (int64_t)n;
    return true;
}

static bool sg_row(Abstractformat *f, xx_pd_struct *pd, uint32_t at, uint32_t size, unsigned width, unsigned bpc)
{
    fm_bytes r;
    unsigned done = 0;
    if (!fm_start(&r, f, pd, at, (int64_t)at + size)) return false;
    for (;;) {
        uint8_t op, high = 0;
        unsigned n;
        if (bpc == 2 && (!fm_byte(&r, &high) || high)) return fm_finish(&r, false);
        if (!fm_byte(&r, &op)) {
            return fm_finish(&r, false);
        }
        n = op & 127;
        if (!n) return fm_finish(&r, done == width && r.pos == r.end);
        if (n > width - done || !fm_skip(&r, (uint64_t)((op & 128) ? n : 1U) * bpc)) {
            return fm_finish(&r, false);
        }
        done += n;
    }
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[512], word[4];
    unsigned x, y, z, dim, bpc, i, j, count;
    uint32_t starts[1024], sizes[1024];
    int64_t end;
    uint64_t samples, plane;
    char label[48];
    bool named = false;
    if (!pm_read(f, 0, h, 512) || xx_data_get_u16(h, 2, 0, true) != 474 || h[2] > 1 || (h[3] != 1 && h[3] != 2) || xx_data_get_u32(h + 104, 4, 0, true)) return false;
    bpc = h[3];
    dim = xx_data_get_u16(h + 4, 2, 0, true);
    x = xx_data_get_u16(h + 6, 2, 0, true);
    y = xx_data_get_u16(h + 8, 2, 0, true);
    z = xx_data_get_u16(h + 10, 2, 0, true);
    samples = (uint64_t)x * y * z;
    plane = (uint64_t)x * y * bpc;
    if (!x || !y || !z || z > 16 || dim < 1 || dim > 3 || (dim == 1 && (y != 1 || z != 1)) || (dim == 2 && z != 1) || samples > 67108864 || samples * bpc > 268435456 ||
        xx_data_get_u32(h + 12, 4, 0, true) > xx_data_get_u32(h + 16, 4, 0, true) || xx_data_get_u32(h + 16, 4, 0, true) > (bpc == 1 ? 255U : 65535U))
        return false;
    for (i = 24; i < 104; ++i)
        if (!h[i]) named = true;
    if (!named || !pm_add(f, s, "descriptor.bin", 0, 512)) return false;
    if (!h[2]) {
        end = 512 + (int64_t)(plane * z);
        if (end > pm_available(f)) return false;
        for (i = 0; i < z; ++i) {
            if (pd && xx_pd_is_stopped(pd)) return false;
            xx_rt_snprintf(label, sizeof(label), "channel-%u.bin", i);
            if (!pm_add(f, s, label, 512 + (int64_t)(plane * i), (int64_t)plane)) return false;
        }
    } else {
        count = y * z;
        if (count > 1024) return false;
        end = 512 + 8 * (int64_t)count;
        if (end > pm_available(f)) return false;
        for (i = 0; i < count; ++i) {
            if ((pd && xx_pd_is_stopped(pd)) || !pm_read(f, 512 + 4 * (int64_t)i, word, 4)) {
                return false;
            }
            starts[i] = xx_data_get_u32(word, 4, 0, true);
            if (!pm_read(f, 512 + 4 * (int64_t)(count + i), word, 4)) {
                return false;
            }
            sizes[i] = xx_data_get_u32(word, 4, 0, true);
            if (starts[i] < 512U + 8U * count || !sizes[i] || sizes[i] > (2U * x + 1U) * bpc || (uint64_t)starts[i] + sizes[i] > (uint64_t)pm_available(f)) return false;
            for (j = 0; j < i; ++j) {
                if ((uint64_t)starts[i] < (uint64_t)starts[j] + sizes[j] && (uint64_t)starts[j] < (uint64_t)starts[i] + sizes[i] &&
                    !(starts[i] == starts[j] && sizes[i] == sizes[j]))
                    return false;
            }
            if (!sg_row(f, pd, starts[i], sizes[i], x, bpc)) return false;
            if ((int64_t)starts[i] + sizes[i] > end) end = (int64_t)starts[i] + sizes[i];
            xx_rt_snprintf(label, sizeof(label), "channel-%u-row-%u.sgi-rle", i / y, i % y);
            if (!pm_add(f, s, label, starts[i], sizes[i])) return false;
        }
    }
    s->size = end;
    return true;
}

void xx_sgi_rgb_init(xx_sgi_rgb *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_SGI_RGB, "sgi");
    }
}
xx_sgi_rgb *xx_sgi_rgb_create(xx_io_device *d, int64_t b)
{
    xx_sgi_rgb *r = (xx_sgi_rgb *)xx_mem_alloc(sizeof(*r));
    if (r) xx_sgi_rgb_init(r, d, b);
    return r;
}
void xx_sgi_rgb_destroy(xx_sgi_rgb *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_sgi_rgb_free(xx_sgi_rgb *r)
{
    if (r) {
        xx_sgi_rgb_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_sgi_rgb_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_sgi_rgb_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
