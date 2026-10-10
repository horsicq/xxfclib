/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://floyd.lbl.gov/radiance/refer/filefmts.pdf, https://raw.githubusercontent.com/NREL/Radiance/master/src/common/color.c
 * Stored encoded component extraction; no image rendering or execution.
 */
#include "xxfclib/formats/radiance_hdr/xx_radiance_hdr.h"
#include "../xx_payload_members.h"
#include "xxfclib/global/xx_global.h"

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

static bool rd_line(fm_bytes *r, char *line, size_t capacity)
{
    size_t n = 0;
    uint8_t b;
    while (fm_byte(r, &b)) {
        if (b == 10) {
            if (n && line[n - 1] == 13) --n;
            line[n] = 0;
            return true;
        }
        if (n + 1 >= capacity || (b < 32 && b != 9 && b != 13) || b > 126) {
            return false;
        }
        line[n++] = (char)b;
    }
    return false;
}
static bool rd_number(const char **p, unsigned *n)
{
    unsigned value = 0, digits = 0;
    while (**p == ' ' || **p == '\t') ++*p;
    while (**p >= '0' && **p <= '9') {
        if (value > 3276 || (value == 3276 && **p > '7')) return false;
        value = value * 10U + (unsigned)(*(*p)++ - '0');
        ++digits;
    }
    if (!digits || !value) {
        return false;
    }
    *n = value;
    return true;
}
static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    fm_bytes r;
    char line[1025], label[48];
    unsigned width, height, i;
    bool format = false;
    int64_t start;
    const char *p;
    if (!fm_start(&r, f, pd, 0, pm_available(f))) return false;
    if (!rd_line(&r, line, sizeof(line)) || (xx_rt_strcmp(line, "#?RADIANCE") && xx_rt_strcmp(line, "#?RGBE"))) return fm_finish(&r, false);
    for (;;) {
        if (r.pos > 65536 || !rd_line(&r, line, sizeof(line))) return fm_finish(&r, false);
        if (!line[0]) break;
        if (!xx_rt_strncmp(line, "FORMAT=", 7)) {
            if (format || (xx_rt_strcmp(line + 7, "32-bit_rle_rgbe") && xx_rt_strcmp(line + 7, "32-bit_rle_xyze"))) {
                return fm_finish(&r, false);
            }
            format = true;
        }
    }
    if (!format || !rd_line(&r, line, sizeof(line)) || r.pos > 65536) return fm_finish(&r, false);
    p = line;
    if ((*p != '-' && *p != '+') || p[1] != 'Y' || p[2] != ' ') {
        return fm_finish(&r, false);
    }
    p += 3;
    if (!rd_number(&p, &height)) {
        return fm_finish(&r, false);
    }
    while (*p == ' ' || *p == '\t') ++p;
    if ((*p != '+' && *p != '-') || p[1] != 'X' || p[2] != ' ') {
        return fm_finish(&r, false);
    }
    p += 3;
    if (!rd_number(&p, &width)) {
        return fm_finish(&r, false);
    }
    while (*p == ' ' || *p == '\t') ++p;
    if (*p || height > 4096 || (uint64_t)width * height > 16777216 || !pm_add(f, s, "descriptor.txt", 0, r.pos)) return fm_finish(&r, false);
    for (i = 0; i < height; ++i) {
        uint8_t h[4];
        unsigned c, j;
        bool encoded;
        start = r.pos;
        if (pd && xx_pd_is_stopped(pd)) return fm_finish(&r, false);
        for (j = 0; j < 4; ++j)
            if (!fm_byte(&r, &h[j])) return fm_finish(&r, false);
        encoded = width >= 8 && h[0] == 2 && h[1] == 2 && !(h[2] & 128);
        if (encoded) {
            if (((unsigned)h[2] << 8 | h[3]) != width) return fm_finish(&r, false);
            for (c = 0; c < 4; ++c) {
                unsigned done = 0;
                while (done < width) {
                    uint8_t op;
                    unsigned n;
                    if (!fm_byte(&r, &op) || !op) {
                        return fm_finish(&r, false);
                    }
                    n = op > 128 ? op - 128U : op;
                    if (n > width - done || !fm_skip(&r, op > 128 ? 1U : n)) {
                        return fm_finish(&r, false);
                    }
                    done += n;
                }
            }
        } else {
            if (h[0] == 1 && h[1] == 1 && h[2] == 1) return fm_finish(&r, false);
            for (j = 1; j < width; ++j) {
                for (c = 0; c < 4; ++c)
                    if (!fm_byte(&r, &h[c])) return fm_finish(&r, false);
                if (h[0] == 1 && h[1] == 1 && h[2] == 1) return fm_finish(&r, false);
            }
        }
        xx_rt_snprintf(label, sizeof(label), "row-%u.%s", i, encoded ? "radiance-rle" : "rgbe");
        if (!pm_add(f, s, label, start, r.pos - start)) return fm_finish(&r, false);
    }
    s->size = r.pos;
    return fm_finish(&r, true);
}

void xx_radiance_hdr_init(xx_radiance_hdr *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_RADIANCE_HDR, "hdr");
    }
}
xx_radiance_hdr *xx_radiance_hdr_create(xx_io_device *d, int64_t b)
{
    xx_radiance_hdr *r = (xx_radiance_hdr *)xx_mem_alloc(sizeof(*r));
    if (r) xx_radiance_hdr_init(r, d, b);
    return r;
}
void xx_radiance_hdr_destroy(xx_radiance_hdr *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_radiance_hdr_free(xx_radiance_hdr *r)
{
    if (r) {
        xx_radiance_hdr_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_radiance_hdr_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_radiance_hdr_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
