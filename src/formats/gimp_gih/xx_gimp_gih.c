/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independently implemented from: https://developer.gimp.org/core/standards/gih/, https://raw.githubusercontent.com/GNOME/gimp/master/libgimpbase/gimpparasiteio.c
 * Stored encoded components only; no rendering or external-resource access.
 */
#include "xxfclib/formats/gimp_gih/xx_gimp_gih.h"
#include "../common/xx_binary_cursor.h"

static bool sm_gimp(Abstractformat *f, uint64_t at, uint64_t end, bool pattern, uint64_t *header, uint64_t *stop, xx_pd_struct *pd)
{
    uint8_t h[28], name[4096];
    uint32_t hs, w, height, channels, fixed = pattern ? 24 : 28;
    uint64_t pixels, bytes;
    size_t i, n;
    if (binary_stop(pd) || !binary_range(at, fixed, end) || !pm_read(f, (int64_t)at, h, fixed)) return false;
    hs = xx_data_get_u32(h, 4, 0, true);
    w = xx_data_get_u32(h + 8, 4, 0, true);
    height = xx_data_get_u32(h + 12, 4, 0, true);
    channels = xx_data_get_u32(h + 16, 4, 0, true);
    if (xx_data_get_u32(h + 4, 4, 0, true) != (pattern ? 1U : 2U) || xx_rt_memcmp(h + 20, pattern ? "GPAT" : "GIMP", 4) || hs <= fixed || hs - fixed > 4096 || !w ||
        !height || w > 32768 || height > 32768 || channels < 1 || channels > 4 || (!pattern && channels != 1 && channels != 4))
        return false;
    if (!binary_mul(w, height, &pixels) || pixels > 67108864 || !binary_mul(pixels, channels, &bytes) || bytes > 268435456 || !binary_range(at, hs + bytes, end))
        return false;
    n = hs - fixed;
    if (!pm_read(f, (int64_t)at + fixed, name, n) || name[n - 1] || !bounded_utf8(name, n - 1, pd)) return false;
    for (i = 0; i < n - 1; ++i)
        if (!name[i]) return false;
    *header = at + hs;
    *stop = *header + bytes;
    return true;
}

static bool sm_decimal(const uint8_t *p, size_t n, uint32_t *out)
{
    size_t i;
    uint32_t v = 0;
    if (!n || n > 7) return false;
    for (i = 0; i < n; ++i) {
        if (p[i] < '0' || p[i] > '9' || v > 1000000) return false;
        v = v * 10 + p[i] - '0';
    }
    *out = v;
    return true;
}
static bool sm_gih_header(const uint8_t *p, size_t n, size_t *header, uint32_t *brushes)
{
    size_t at = 0, line, start, key, colon;
    uint32_t vals[9] = {0}, seen = 0, count, rank = 0;
    bool select = false;
    while (at < n && p[at] != '\n') {
        if (!p[at] || p[at] < 32 || ++at > 128) return false;
    }
    if (!at || at == n) return false;
    line = ++at;
    while (at < n && p[at] != '\n') {
        if (p[at] < 32 || p[at] > 126) return false;
        ++at;
    }
    if (at == n || at + 1 > 511) return false;
    *header = at + 1;
    start = line;
    while (line < at && p[line] >= '0' && p[line] <= '9') ++line;
    if (!sm_decimal(p + start, line - start, &count) || !count || count > 4096 || line == at || p[line++] != ' ') return false;
    while (line < at) {
        size_t value, end;
        uint32_t v = 0;
        unsigned which = 9;
        while (line < at && p[line] == ' ') {
            ++line;
        }
        if (line == at) break;
        key = line;
        while (line < at && p[line] != ':' && p[line] != ' ') ++line;
        colon = line;
        if (line == at || p[line++] != ':') {
            return false;
        }
        value = line;
        while (line < at && p[line] != ' ') ++line;
        end = line;
        if (colon - key == 6 && !xx_rt_memcmp(p + key, "ncells", 6)) which = 0;
        else if (colon - key == 9 && !xx_rt_memcmp(p + key, "cellwidth", 9)) which = 1;
        else if (colon - key == 10 && !xx_rt_memcmp(p + key, "cellheight", 10)) which = 2;
        else if (colon - key == 4 && !xx_rt_memcmp(p + key, "step", 4)) which = 3;
        else if (colon - key == 3 && !xx_rt_memcmp(p + key, "dim", 3)) which = 4;
        else if (colon - key == 4 && !xx_rt_memcmp(p + key, "cols", 4)) which = 5;
        else if (colon - key == 4 && !xx_rt_memcmp(p + key, "rows", 4)) which = 6;
        else if (colon - key == 5 && !xx_rt_memcmp(p + key, "rank0", 5)) which = 7;
        else if (colon - key == 9 && !xx_rt_memcmp(p + key, "placement", 9)) {
            if (seen & 256 || end - value != 8 || xx_rt_memcmp(p + value, "constant", 8)) return false;
            seen |= 256;
            continue;
        } else if (colon - key == 4 && !xx_rt_memcmp(p + key, "sel0", 4)) {
            if (select || !((end - value == 11 && !xx_rt_memcmp(p + value, "incremental", 11)) || (end - value == 6 && !xx_rt_memcmp(p + value, "random", 6)) ||
                            (end - value == 8 && !xx_rt_memcmp(p + value, "pressure", 8))))
                return false;
            select = true;
            continue;
        }
        if (which > 7 || seen & (1U << which) || !sm_decimal(p + value, end - value, &v) || !v) {
            return false;
        }
        vals[which] = v;
        seen |= 1U << which;
    }
    if (!(seen & 1) || vals[0] != count || !(seen & 16) || vals[4] != 1 || !(seen & 128) || (rank = vals[7]) != count || !select ||
        ((seen & 96) == 96 && (uint64_t)vals[5] * vals[6] < count))
        return false;
    *brushes = count;
    return true;
}
bool xx_gimp_gih_probe_header(const uint8_t *p, size_t n)
{
    size_t header;
    uint32_t count;
    return p && sm_gih_header(p, n, &header, &count);
}

static bool pm_parse(Abstractformat *f, pm_stream *s, xx_pd_struct *pd)
{
    uint8_t h[512];
    uint64_t end = (uint64_t)pm_available(f), at, hs, stop;
    size_t n = end > 512 ? 512 : (size_t)end, header;
    uint32_t count, i;
    if (!pm_read(f, 0, h, n) || !sm_gih_header(h, n, &header, &count) || !pm_add(f, s, "gih-header.txt", 0, (int64_t)header)) {
        return false;
    }
    at = header;
    for (i = 0; i < count; ++i) {
        char label[48];
        if (!sm_gimp(f, at, end, false, &hs, &stop, pd) || stop - header > 268435456) return false;
        xx_rt_snprintf(label, sizeof(label), "brush-%u.gbr", i);
        if (!pm_add(f, s, label, (int64_t)at, (int64_t)(stop - at))) return false;
        at = stop;
    }
    s->size = (int64_t)at;
    return true;
}

void xx_gimp_gih_init(xx_gimp_gih *r, xx_io_device *d, int64_t b)
{
    if (r) {
        xx_mem_zero(r, sizeof(*r));
        pm_init(&r->format, d, b, XX_FILE_TYPE_GIMP_GIH, "gih");
    }
}
xx_gimp_gih *xx_gimp_gih_create(xx_io_device *d, int64_t b)
{
    xx_gimp_gih *r = (xx_gimp_gih *)xx_mem_alloc(sizeof(*r));
    if (r) xx_gimp_gih_init(r, d, b);
    return r;
}
void xx_gimp_gih_destroy(xx_gimp_gih *r)
{
    if (r) xx_format_cleanup_extra_parameters(&r->format);
}
void xx_gimp_gih_free(xx_gimp_gih *r)
{
    if (r) {
        xx_gimp_gih_destroy(r);
        xx_mem_free(r);
    }
}
bool xx_gimp_gih_check_is_valid(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_valid(f, pd);
}
bool xx_gimp_gih_handle_base_info(Abstractformat *f, xx_pd_struct *pd)
{
    return pm_handle(f, pd);
}
