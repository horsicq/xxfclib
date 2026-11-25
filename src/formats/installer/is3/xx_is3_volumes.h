/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Private, conservative complete-set view. Wire fields independently
 * described by lephilousophe/idecomp; no reference decoder code is used. */
#ifndef XX_IS3_VOLUMES_H
#define XX_IS3_VOLUMES_H
#include "../../../fs/platforms/xx_fs_platform.h"
#define IS3_SET_LIMIT (INT64_C(512) * 1024 * 1024)
#define IS3_TABLE_LIMIT (4U * 1024 * 1024)
typedef struct is3_volume_scan {
    const char *directory, *selected;
    uint8_t identity[26], headers[32][63];
    xx_io_device *devices[32];
    int64_t sizes[32], bytes;
    unsigned highest, seen;
    bool selected_seen;
    xx_pd_struct *pd;
} is3_volume_scan;
static uint32_t is3_v32(const uint8_t *p)
{
    return xx_data_get_u32(p, 4, 0, false);
}
static void is3_vput(uint8_t *p, uint32_t v, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8U * i));
}
static bool is3_volume_candidate(void *opaque, const char *name, bool directory)
{
    is3_volume_scan *s = (is3_volume_scan *)opaque;
    xx_io_device *d;
    char *path;
    uint8_t h[63];
    unsigned part;
    int64_t n;
    if (++s->seen > 4096U || xx_pd_is_stopped(s->pd)) return false;
    if (directory) return true;
    if (xx_str_len(s->directory) + xx_str_len(name) > 4094U) return false;
    path = xx_str_concat3(s->directory, "/", name);
    if (!path) return false;
    d = xx_io_file_open(path, "rb");
    xx_str_free(path);
    if (!d) return true;
    if (!xx_io_read_at(d, 0, h, sizeof(h)) || xx_rt_memcmp(h, s->identity, sizeof(s->identity))) {
        xx_io_close(d);
        return true;
    }
    n = xx_io_total_size(d);
    part = h[31];
    if (!part || part > 32U || s->devices[part - 1] || n < 255 || n > IS3_SET_LIMIT - s->bytes) {
        xx_io_close(d);
        return false;
    }
    s->devices[part - 1] = d;
    s->sizes[part - 1] = n;
    s->bytes += n;
    xx_mem_copy(s->headers[part - 1], h, sizeof(h));
    if (part > s->highest) s->highest = part;
    if (!xx_rt_strcmp(name, s->selected)) s->selected_seen = true;
    return true;
}
static bool is3_volume_write(xx_io_device *d, const void *data, size_t n)
{
    const uint8_t *p = (const uint8_t *)data;
    while (n) {
        ssize_t sent = xx_io_write(d, p, n);
        if (sent <= 0 || (size_t)sent > n) return false;
        p += sent;
        n -= (size_t)sent;
    }
    return true;
}
static bool is3_volume_copy(xx_io_device *out, xx_io_device *in, uint64_t at, uint64_t n, xx_pd_struct *pd)
{
    uint8_t b[32768];
    while (n) {
        size_t take = n > sizeof(b) ? sizeof(b) : (size_t)n;
        if (xx_pd_is_stopped(pd) || !xx_io_read_at(in, (int64_t)at, b, take) || !is3_volume_write(out, b, take)) return false;
        at += take;
        n -= take;
    }
    return true;
}
static bool is3_volume_equal(xx_io_device *d, uint64_t at, const uint8_t *p, size_t n, xx_pd_struct *pd)
{
    uint8_t b[4096];
    while (n) {
        size_t take = n > sizeof(b) ? sizeof(b) : n;
        if (xx_pd_is_stopped(pd) || !xx_io_read_at(d, (int64_t)at, b, take) || xx_rt_memcmp(b, p, take)) return false;
        at += take;
        p += take;
        n -= take;
    }
    return true;
}
static xx_io_device *is3_volume_view(Abstractformat *f, xx_pd_struct *pd)
{
    is3_volume_scan s;
    uint8_t selected[63], head[255], *dirs = NULL, *table = NULL, *other = NULL;
    const char *source, *p;
    char *directory = NULL;
    size_t prefix = 0, at;
    uint32_t ds, fs, dir_at, file_at, total, raw_total = 0;
    uint64_t packed_total = 0;
    unsigned parts, count, i, j, previous_part = 1;
    uint64_t previous_offset = 255;
    xx_io_device *out = NULL;
    bool ok = false;
    if (f->base_address || !(source = xx_io_source_path(f->device)) || xx_str_len(source) > 4094U || !xx_io_read_at(f->device, 0, selected, sizeof(selected)) ||
        is3_v32(selected) != IS3_SIGNATURE || selected[4] != 0x3a || selected[9] || xx_data_get_u16(selected + 10, 2, 0, false) != 1U)
        return NULL;
    for (p = source; *p; p++)
        if (*p == '/' || *p == '\\') prefix = (size_t)(p - source) + 1U;
    directory = prefix ? xx_str_create_len(prefix) : xx_str_dup(".");
    if (!directory) return NULL;
    if (prefix) {
        xx_mem_copy(directory, source, prefix);
        directory[prefix] = 0;
    }
    xx_mem_zero(&s, sizeof(s));
    s.directory = directory;
    s.selected = source + prefix;
    s.pd = pd;
    xx_mem_copy(s.identity, selected, sizeof(s.identity));
    if (!xx_fs_platform_enumerate(directory, is3_volume_candidate, &s) || !s.selected_seen || !s.devices[0]) goto done;
    parts = s.headers[0][30];
    count = xx_data_get_u16(s.headers[0] + 12, 2, 0, false);
    if (parts < 1 || parts > 32 || s.highest != parts || !count) goto done;
    ds = is3_v32(s.headers[0] + 45);
    fs = is3_v32(s.headers[0] + 55);
    dir_at = is3_v32(s.headers[0] + 41);
    file_at = is3_v32(s.headers[0] + 51);
    total = is3_v32(s.headers[0] + 18);
    if (!ds || !fs || ds > IS3_TABLE_LIMIT || fs > IS3_TABLE_LIMIT || ds + fs > IS3_TABLE_LIMIT || total > IS3_SET_LIMIT || total % 253U != s.headers[0][32]) goto done;
    dirs = (uint8_t *)xx_mem_alloc(ds);
    table = (uint8_t *)xx_mem_alloc(fs);
    other = (uint8_t *)xx_mem_alloc(fs);
    if (!dirs || !table || !other || !xx_io_read_at(s.devices[0], dir_at, dirs, ds) || !xx_io_read_at(s.devices[0], file_at, table, fs)) goto done;
    for (i = 0; i < parts; i++) {
        uint8_t *h = s.headers[i];
        uint32_t da = is3_v32(h + 41), fa = is3_v32(h + 51);
        if (!s.devices[i] || (i && h[30]) || h[32] != s.headers[0][32] || is3_v32(h + 45) != ds || is3_v32(h + 55) != fs || xx_rt_memcmp(h + 49, s.headers[0] + 49, 2) ||
            da < 255 || (uint64_t)da + ds != fa || (uint64_t)fa + fs != (uint64_t)s.sizes[i] || !is3_volume_equal(s.devices[i], da, dirs, ds, pd) ||
            !xx_io_read_at(s.devices[i], fa, other, fs))
            goto done;
        at = 0;
        for (j = 0; j < count; j++) {
            unsigned k, n;
            uint16_t flags_a, flags_b;
            if (fs - at < 30) goto done;
            n = xx_data_get_u16(table + at + 23, 2, 0, false);
            if (n < 43U + table[at + 29] || n > fs - at || n != xx_data_get_u16(other + at + 23, 2, 0, false)) goto done;
            flags_a = xx_data_get_u16(table + at + 25, 2, 0, false);
            flags_b = xx_data_get_u16(other + at + 25, 2, 0, false);
            if ((flags_a ^ flags_b) & ~0x140U) goto done;
            for (k = 0; k < n; k++) {
                if (k == 0 || (k >= 11 && k < 15) || k == 25 || k == 26 || k == 28) continue;
                if (table[at + k] != other[at + k]) goto done;
            }
            at += n;
        }
        if (at != fs) goto done;
    }
    if (!xx_io_read_at(s.devices[0], 0, head, sizeof(head))) goto done;
    out = xx_io_temp_open();
    if (!out || !is3_volume_write(out, head, sizeof(head))) goto done;
    at = 0;
    for (i = 0; i < count; i++) {
        uint8_t *record = table + at;
        unsigned start = record[28], end = record[0], part;
        uint16_t flags = xx_data_get_u16(record + 25, 2, 0, false), entry = xx_data_get_u16(record + 23, 2, 0, false);
        uint32_t packed = is3_v32(record + 7), raw = is3_v32(record + 3), offset = is3_v32(record + 11);
        uint64_t copied = 0;
        int64_t logical = xx_io_tell(out);
        if (xx_pd_is_stopped(pd) || !start || start > end || end > parts || !packed || raw > 64U * 1024 * 1024 || logical < 255 || logical > IS3_SET_LIMIT ||
            ((flags & 0x100U) != 0) != (start != end))
            goto done;
        if (start != previous_part || offset != previous_offset) {
            if (start != previous_part + 1U || previous_offset != is3_v32(s.headers[previous_part - 1] + 41) || offset != 255) goto done;
        }
        for (part = start; part <= end; part++) {
            uint64_t begin = part == start ? offset : 255U, limit;
            if (start == end) limit = begin + packed;
            else if (part == end) limit = is3_v32(s.headers[part - 1] + 37);
            else limit = is3_v32(s.headers[part - 1] + 41);
            if (begin < 255 || limit < begin || limit > is3_v32(s.headers[part - 1] + 41) || limit - begin > packed - copied ||
                !is3_volume_copy(out, s.devices[part - 1], begin, limit - begin, pd))
                goto done;
            copied += limit - begin;
            previous_part = part;
            previous_offset = limit;
        }
        if (copied != packed || packed_total > (uint64_t)IS3_SET_LIMIT - packed || raw_total > UINT32_MAX - raw) goto done;
        packed_total += packed;
        raw_total += raw;
        is3_vput(record + 11, (uint32_t)logical, 4);
        is3_vput(record + 25, flags & ~0x100U, 2);
        record[0] = record[28] = 1;
        at += entry;
    }
    if (previous_part != parts || previous_offset != is3_v32(s.headers[parts - 1] + 41) || 255U + packed_total + ds + fs != total || raw_total != is3_v32(head + 22))
        goto done;
    dir_at = (uint32_t)xx_io_tell(out);
    file_at = dir_at + ds;
    if (!is3_volume_write(out, dirs, ds) || !is3_volume_write(out, table, fs) || xx_io_tell(out) != total) goto done;
    is3_vput(head + 10, 0, 2);
    is3_vput(head + 26, 255, 4);
    head[30] = head[31] = 1;
    is3_vput(head + 33, (uint32_t)packed_total, 4);
    is3_vput(head + 37, 255, 4);
    is3_vput(head + 41, dir_at, 4);
    is3_vput(head + 51, file_at, 4);
    if (xx_io_seek64(out, 0, SEEK_SET) || !is3_volume_write(out, head, sizeof(head)) || xx_io_seek64(out, 0, SEEK_SET)) goto done;
    ok = true;
done:
    for (i = 0; i < 32; i++)
        if (s.devices[i]) xx_io_close(s.devices[i]);
    xx_mem_free(dirs);
    xx_mem_free(table);
    xx_mem_free(other);
    xx_str_free(directory);
    if (!ok && out) {
        xx_io_close(out);
        out = NULL;
    }
    return out;
}
#endif
