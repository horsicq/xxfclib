/* SPDX-License-Identifier: MIT
 * Primary reference: https://pubs.usgs.gov/dug/0005/dug0005.pdf
 * USGS ASCII DEM: complete fixed1024-byte A descriptor and counted B elevation profiles/continuations, finite header/profile numbers, exact sample extents and matching elevation extrema. Optional LF/CRLF physical record separators accepted. Original descriptor/profile elevation records exported; C accuracy records, rotated grids and unsupported datums/projection/profile layouts declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#include "xxfclib/formats/usgs_dem/xx_usgs_dem.h"
#include "../common/xx_component_text.h"

static bool mesh_font_parse(Abstractformat *, pm_stream *, const uint8_t *, uint64_t, xx_pd_struct *);
static bool mesh_font_quick(Abstractformat *, uint64_t);
XX_COMPONENT_CHUNKED_READ_DRIVER(mesh_font, 33554432, if (ok) s->size = available;)
typedef struct mesh_font_dem {
    const uint8_t *b;
    uint64_t logical, n;
    unsigned stride;
    xx_pd_struct *pd;
} mesh_font_dem;
static bool mesh_font_quick(Abstractformat *f, uint64_t n) {
    uint8_t c;
    return n >= 2048 && pm_read(f, 0, &c, 1) && c >= 32 && c <= 126;
}
static uint8_t mesh_font_dem_byte(mesh_font_dem *q, uint64_t at) { return q->b[(at / 1024) * q->stride + at % 1024]; }
static bool mesh_font_dem_int(mesh_font_dem *q, uint64_t at, unsigned z, int32_t *v) {
    uint8_t b[32];
    unsigned i;
    component_text_cursor t;
    if (z > sizeof(b) || !component_span(at, z, q->logical) || xx_component_parser_stopped(q->pd))
        return false;
    for (i = 0; i < z; ++i)
        b[i] = mesh_font_dem_byte(q, at + i);
    t.b = b;
    t.t = 0;
    t.stop = z;
    return component_text_integer(&t, v) && component_text_done(&t);
}
static bool mesh_font_dem_real(mesh_font_dem *q, uint64_t at, unsigned z, double *v, bool blank) {
    uint8_t b[32];
    unsigned i;
    component_text_cursor t;
    bool empty = true;
    if (z > sizeof(b) || !component_span(at, z, q->logical) || xx_component_parser_stopped(q->pd))
        return false;
    for (i = 0; i < z; ++i) {
        b[i] = mesh_font_dem_byte(q, at + i);
        if (b[i] != ' ')
            empty = false;
        if (b[i] == 'D' || b[i] == 'd')
            b[i] = 'E';
    }
    if (empty && blank) {
        *v = 0;
        return true;
    }
    t.b = b;
    t.t = 0;
    t.stop = z;
    return component_text_number_36_digits(&t, v) && component_text_done(&t);
}
static bool mesh_font_dem_pad(mesh_font_dem *q, uint64_t p, uint64_t end) {
    for (; p < end; ++p)
        if (mesh_font_dem_byte(q, p) != 32 || ((p & 4095) == 0 && xx_component_parser_stopped(q->pd)))
            return false;
    return true;
}
static bool mesh_font_parse(Abstractformat *f, pm_stream *s, const uint8_t *b, uint64_t n, xx_pd_struct *pd) {
    mesh_font_dem q = {b, 0, n, 1024, pd};
    uint64_t p = 1024, i;
    unsigned x;
    int32_t value, profiles;
    double v, min, max, resx, resy, resz;
    char label[64];
    if (n < 2048) {
        return false;
    }
    if (n > 1024 && b[1024] == 10)
        q.stride = 1025;
    else if (n > 1025 && b[1024] == 13 && b[1025] == 10)
        q.stride = 1026;
    if (n % q.stride || n / q.stride < 2) {
        return false;
    }
    q.logical = n / q.stride * 1024;
    for (i = 0; i < n; ++i) {
        unsigned at = (unsigned)(i % q.stride);
        if (at < 1024) {
            if (b[i] < 32 || b[i] > 126)
                return false;
        } else if ((q.stride == 1025 && b[i] != 10) || (q.stride == 1026 && b[i] != (at == 1024 ? 13 : 10)))
            return false;
        if ((i & 4095) == 0 && xx_component_parser_stopped(pd))
            return false;
    }
    if (!mesh_font_dem_int(&q, 144, 6, &value) || value < 1 || value > 3 || !mesh_font_dem_int(&q, 150, 6, &value) ||
        value != 1 || !mesh_font_dem_int(&q, 156, 6, &value) || value < 0 || value > 2 ||
        !mesh_font_dem_int(&q, 162, 6, &value) || value < -60 || value > 60)
        return false;
    for (i = 0; i < 15; ++i)
        if (!mesh_font_dem_real(&q, 168 + i * 24, 24, &v, true))
            return false;
    if (!mesh_font_dem_int(&q, 528, 6, &value) || value < 0 || value > 3 || !mesh_font_dem_int(&q, 534, 6, &value) ||
        value < 1 || value > 2 || !mesh_font_dem_int(&q, 540, 6, &value) || value != 4)
        return false;
    for (i = 0; i < 8; ++i)
        if (!mesh_font_dem_real(&q, 546 + i * 24, 24, &v, false))
            return false;
    if (!mesh_font_dem_real(&q, 738, 24, &min, false) || !mesh_font_dem_real(&q, 762, 24, &max, false) || min > max ||
        !mesh_font_dem_real(&q, 786, 24, &v, false) || v != 0 || !mesh_font_dem_int(&q, 810, 6, &value) || value != 0 ||
        !mesh_font_dem_real(&q, 816, 12, &resx, false) || resx <= 0 || !mesh_font_dem_real(&q, 828, 12, &resy, false) ||
        resy <= 0 || !mesh_font_dem_real(&q, 840, 12, &resz, false) || resz <= 0 ||
        !mesh_font_dem_int(&q, 852, 6, &value) || value != 1 || !mesh_font_dem_int(&q, 858, 6, &profiles) ||
        profiles < 1 || profiles > 4000 || !component_emit(f, s, "descriptor.dem", 0, q.stride, n))
        return false;
    for (x = 0; x < (unsigned)profiles; ++x) {
        uint64_t start = p, stop;
        int32_t rows, column;
        double offset, lo, hi, actualmin = 0, actualmax = 0;
        bool samples = false;
        if (xx_component_parser_stopped(pd) || !mesh_font_dem_int(&q, p, 6, &value) || value != 1 ||
            !mesh_font_dem_int(&q, p + 6, 6, &column) || column != (int32_t)x + 1 ||
            !mesh_font_dem_int(&q, p + 12, 6, &rows) || rows < 1 || rows > 1000000 ||
            !mesh_font_dem_int(&q, p + 18, 6, &value) || value != 1)
            return false;
        if (!mesh_font_dem_real(&q, p + 24, 24, &v, false) || !mesh_font_dem_real(&q, p + 48, 24, &v, false) ||
            !mesh_font_dem_real(&q, p + 72, 24, &offset, false) || !mesh_font_dem_real(&q, p + 96, 24, &lo, false) ||
            !mesh_font_dem_real(&q, p + 120, 24, &hi, false) || lo > hi) {
            return false;
        }
        p += 144;
        for (i = 0; i < (uint64_t)rows; ++i) {
            if (p % 1024 > 1018) {
                stop = (p + 1023) / 1024 * 1024;
                if (stop > q.logical || !mesh_font_dem_pad(&q, p, stop))
                    return false;
                p = stop;
            }
            if (!mesh_font_dem_int(&q, p, 6, &value))
                return false;
            p += 6;
            if (value != -32767) {
                v = value * resz + offset;
                if (!samples) {
                    actualmin = actualmax = v;
                    samples = true;
                } else {
                    if (v < actualmin)
                        actualmin = v;
                    if (v > actualmax)
                        actualmax = v;
                }
            }
        }
        if (!samples || !component_near(lo, actualmin) || !component_near(hi, actualmax)) {
            return false;
        }
        stop = (p + 1023) / 1024 * 1024;
        if (stop > q.logical || !mesh_font_dem_pad(&q, p, stop))
            return false;
        p = stop;
        xx_rt_snprintf(label, sizeof(label), "elevation-profile-%u.dem", x);
        if (!component_emit(f, s, label, start / 1024 * q.stride, (p - start) / 1024 * q.stride, n))
            return false;
    }
    return p == q.logical;
}

void xx_usgs_dem_init(xx_usgs_dem *r, xx_io_device *d, int64_t at) {
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, at, XX_FILE_TYPE_USGS_DEM, "dem");
    }
}
xx_usgs_dem *xx_usgs_dem_create(xx_io_device *d, int64_t at) {
    xx_usgs_dem *r = (xx_usgs_dem *)xx_mem_alloc(sizeof(*r));
    if (r)
        xx_usgs_dem_init(r, d, at);
    return r;
}
void xx_usgs_dem_destroy(xx_usgs_dem *r) {
    if (r)
        xx_format_cleanup_extra_parameters(&r->format);
}
void xx_usgs_dem_free(xx_usgs_dem *r) {
    if (r) {
        xx_usgs_dem_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_usgs_dem_check_is_valid(Abstractformat *f, xx_pd_struct *pd) { return pm_valid(f, pd); }
bool xx_usgs_dem_handle_base_info(Abstractformat *f, xx_pd_struct *pd) { return pm_handle(f, pd); }
