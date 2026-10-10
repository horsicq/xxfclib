/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://raw.githubusercontent.com/sarnold/urt/master/lib/rle_getrow.c, https://brlcad.org/OLD/doxygen/d6/d94/rle__code_8h-source.html
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/utah_rle/xx_utah_rle.h"
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

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[15], b, op, datum;
    unsigned width, height, channels, flags, ncmap, x = 0, y = 0, color = 0, instructions = 0;
    uint64_t n;
    fm_bytes r;
    int64_t start;
    bool pixels = false;
    if (!pm_read(f, 0, h, 15) || xx_data_get_u16(h, 2, 0, false) != 0xCC52 || h[12] != 8) return false;
    width = xx_data_get_u16(h + 6, 2, 0, false);
    height = xx_data_get_u16(h + 8, 2, 0, false);
    flags = h[10];
    channels = h[11];
    ncmap = h[13];
    if (!width || !height || width > 32767 || height > 32767 || (uint64_t)width * height > 16777216 || flags > 15 || (flags & 3) == 3 || !channels || channels > 4 ||
        ncmap > 4 || h[14] > 8)
        return false;
    if (!pm_add(f, s, "setup.bin", 0, 15)) return false;
    if (!fm_start(&r, f, pd, 15, pm_available(f))) {
        return false;
    }
    n = flags & 2 ? 1U : 1U + (channels / 2U) * 2U;
    if (!fm_skip(&r, n) || (!(flags & 2) && !pm_add(f, s, "background.bin", 15, channels))) return fm_finish(&r, false);
    if (ncmap) {
        n = (uint64_t)ncmap * ((uint64_t)1 << h[14]) * 2U;
        start = r.pos;
        if (!fm_skip(&r, n) || !pm_add(f, s, "colormap.le16", start, (int64_t)n)) return fm_finish(&r, false);
    }
    if (flags & 8) {
        uint8_t word[2], last;
        unsigned len;
        start = r.pos;
        if (!pm_read(f, start, word, 2)) {
            return fm_finish(&r, false);
        }
        len = xx_data_get_u16(word, 2, 0, false);
        if (!len || !fm_skip(&r, 2U + len + (len & 1U)) || !pm_read(f, start + 1 + len, &last, 1) || last || !pm_add(f, s, "comments.txt", start + 2, len))
            return fm_finish(&r, false);
    }
    start = r.pos;
    for (;;) {
        unsigned code;
        bool long_op;
        if (++instructions > 4000000 || (pd && xx_pd_is_stopped(pd)) || !fm_byte(&r, &op) || !fm_byte(&r, &datum) || (op & 0x80) || (op & 0x38))
            return fm_finish(&r, false);
        long_op = (op & 64) != 0;
        code = op & 63;
        n = datum;
        if (long_op) {
            uint8_t lo, hi;
            if (code == 2 || code == 7 || !fm_byte(&r, &lo) || !fm_byte(&r, &hi)) return fm_finish(&r, false);
            n = (unsigned)lo + ((unsigned)hi << 8);
        }
        if (code == 1) {
            if (!n || n > height - y) return fm_finish(&r, false);
            y += (unsigned)n;
            x = 0;
            color = 0;
        } else if (code == 2) {
            if (datum >= channels && !(datum == 255 && (flags & 4))) return fm_finish(&r, false);
            color = datum;
            x = 0;
        } else if (code == 3) {
            if (y >= height || n > width - x) return fm_finish(&r, false);
            x += (unsigned)n;
        } else if (code == 5 || code == 6) {
            ++n;
            if (y >= height || (color >= channels && color != 255) || n > width - x) return fm_finish(&r, false);
            if (code == 5) {
                if (!fm_skip(&r, n + (n & 1U))) return fm_finish(&r, false);
            } else if (!fm_byte(&r, &b) || !fm_byte(&r, &b)) return fm_finish(&r, false);
            x += (unsigned)n;
            pixels = true;
        } else if (code == 7) {
            if (datum || !pixels) return fm_finish(&r, false);
            break;
        } else return fm_finish(&r, false);
    }
    if (!pm_add(f, s, "opcodes.urt-rle", start, r.pos - start)) {
        return fm_finish(&r, false);
    }
    s->size = r.pos;
    return fm_finish(&r, true);
}

void xx_utah_rle_init(xx_utah_rle *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_UTAH_RLE, "rle");
    }
}
xx_utah_rle *xx_utah_rle_create(xx_io_device *d, int64_t b)
{
    xx_utah_rle *r = (xx_utah_rle *)xx_mem_alloc(sizeof(*r));
    if (r) xx_utah_rle_init(r, d, b);
    return r;
}
void xx_utah_rle_destroy(xx_utah_rle *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_utah_rle_free(xx_utah_rle *r)
{
    if (r) {
        xx_utah_rle_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_utah_rle_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_utah_rle_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
