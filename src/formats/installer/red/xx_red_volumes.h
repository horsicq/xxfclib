/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#ifndef XX_RED_VOLUMES_H
#define XX_RED_VOLUMES_H
#include "../../../fs/platforms/xx_fs_platform.h"
#define RED_JOIN_LIMIT (64U * 1024U * 1024U)

typedef struct red_volume_scan {
    Abstractformat *selected;
    const red_member *member;
    const char *directory, *selected_name;
    unsigned seen, attempts, accepted;
    int64_t bytes;
    uint8_t *plain;
    char *name;
    size_t plain_size;
    uint16_t crc;
    xx_pd_struct *pd;
} red_volume_scan;

/* Segment CRCs belong to each physical fragment: remove them before joining
 * coded bytes. A final full-file raw CRC anchors the cross-volume match. */
static bool red_fragment_copy(xx_io_device *d, const red_member *m, uint8_t *out, size_t *written, xx_pd_struct *pd)
{
    uint8_t block[4096];
    int64_t at = 0;
    while (at < m->packed_size) {
        size_t n = (size_t)(m->packed_size - at);
        if (n > sizeof(block)) n = sizeof(block);
        if (xx_pd_is_stopped(pd) || !red_read_at(d, m->data_offset + at, block, n)) return false;
        at += (int64_t)n;
        if (m->method == RED_METHOD_LH5) {
            if (n < 2U || red_crc16(block, n - 2U) != (uint16_t)((block[n - 2U] << 8U) | block[n - 1U])) return false;
            n -= 2U;
        }
        xx_rt_memcpy(out + *written, block, n);
        *written += n;
    }
    return true;
}

static bool red_volume_candidate(void *opaque, const char *name, bool directory)
{
    red_volume_scan *s = (red_volume_scan *)opaque;
    xx_io_device *d = NULL;
    red_stream *catalog = NULL;
    Abstractformat candidate;
    const red_member *other, *first, *tail;
    uint8_t header[4], *packed = NULL, *plain = NULL;
    size_t size, coded = 0, written = 0;
    int64_t physical;
    char *path;
    bool more = true, decoded;
    if (++s->seen > 4096U || xx_pd_is_stopped(s->pd)) return false;
    if (directory || !xx_rt_strcmp(name, s->selected_name)) return true;
    if (xx_str_len(s->directory) + xx_str_len(name) > 4094U) return false;
    path = xx_str_concat3(s->directory, "/", name);
    if (!path) return false;
    d = xx_io_file_open(path, "rb");
    xx_str_free(path);
    if (!d) return true;
    if (!xx_io_read_at(d, 0, header, sizeof(header)) || header[0] != 'R' || header[1] != 'R' || header[2] != 1U) goto done;
    physical = xx_io_total_size(d);
    if (physical < RED_MIN_HEADER_SIZE || physical > INT64_C(512) * 1024 * 1024 - s->bytes) {
        more = false;
        goto done;
    }
    s->bytes += physical;
    candidate = *s->selected;
    candidate.device = d;
    candidate.base_address = 0;
    if (!red_parse(&candidate, &catalog)) goto done;
    other = s->member->fragment == 1U ? &catalog->items[0] : &catalog->items[catalog->count - 1U];
    first = s->member->fragment == 1U ? s->member : other;
    tail = s->member->fragment == 1U ? other : s->member;
    if (first->fragment != 1U || first->last_fragment != 0U || tail->fragment != 2U || tail->last_fragment != 1U || tail->unpacked_size != 0U || tail->crc != 0xffffU ||
        first->dos_time != tail->dos_time || first->method != tail->method || first->unpacked_size > RED_JOIN_LIMIT || first->packed_size <= 0 ||
        tail->packed_size <= 0 || first->packed_size > RED_JOIN_LIMIT || tail->packed_size > RED_JOIN_LIMIT - first->packed_size)
        goto done;
    if (other->fragment == 1U && other->data_offset + other->packed_size != physical) goto done;
    if (++s->attempts > 32U) {
        more = false;
        goto done;
    }
    size = (size_t)(first->packed_size + tail->packed_size);
    packed = (uint8_t *)xx_mem_alloc(size);
    plain = (uint8_t *)xx_mem_alloc(first->unpacked_size ? (size_t)first->unpacked_size : 1U);
    if (!packed || !plain) {
        more = false;
        goto done;
    }
    if (!red_fragment_copy(s->member->fragment == 1U ? s->selected->device : d, first, packed, &coded, s->pd) ||
        !red_fragment_copy(s->member->fragment == 1U ? d : s->selected->device, tail, packed, &coded, s->pd))
        goto done;
    if (first->method == RED_METHOD_STORE) {
        decoded = coded == first->unpacked_size;
        if (decoded) {
            xx_rt_memcpy(plain, packed, coded);
            written = coded;
        }
    } else decoded = xx_lzh5_decode_memory(packed, coded, plain, (size_t)first->unpacked_size, 5, &written);
    if (!decoded || written != first->unpacked_size || red_crc16(plain, written) != first->crc || xx_pd_is_stopped(s->pd)) goto done;
    if (++s->accepted != 1U) {
        more = false;
        goto done;
    }
    s->name = xx_str_dup(first->name);
    if (!s->name) {
        more = false;
        goto done;
    }
    s->plain = plain;
    s->plain_size = written;
    s->crc = first->crc;
    plain = NULL;
done:
    xx_mem_free(packed);
    xx_mem_free(plain);
    red_stream_free(catalog);
    xx_io_close(d);
    return more;
}

static void red_resolve_fragments(Abstractformat *f, red_stream *catalog, xx_pd_struct *pd)
{
    const char *source, *p;
    char *directory;
    size_t prefix = 0, aggregate = 0, i, j;
    int64_t saved;
    if (f->base_address || !(source = xx_io_source_path(f->device)) || xx_str_len(source) > 4094U) return;
    saved = xx_io_tell(f->device);
    if (saved < 0) return;
    for (p = source; *p; ++p)
        if (*p == '/' || *p == '\\') prefix = (size_t)(p - source) + 1U;
    directory = prefix ? xx_str_create_len(prefix) : xx_str_dup(".");
    if (!directory) return;
    if (prefix) {
        xx_mem_copy(directory, source, prefix);
        directory[prefix] = 0;
    }
    for (i = 0; i < catalog->count && !xx_pd_is_stopped(pd); ++i) {
        red_member *m = &catalog->items[i];
        red_volume_scan s;
        bool unique = true;
        if (!((m->fragment == 1U && !m->last_fragment && i + 1U == catalog->count && m->data_offset + m->packed_size == catalog->archive_size) ||
              (m->fragment == 2U && m->last_fragment == 1U && i == 0U)))
            continue;
        xx_mem_zero(&s, sizeof(s));
        s.selected = f;
        s.member = m;
        s.directory = directory;
        s.selected_name = source + prefix;
        s.pd = pd;
        if (xx_fs_platform_enumerate(directory, red_volume_candidate, &s) && s.accepted == 1U && !xx_pd_is_stopped(pd)) {
            for (j = 0; j < catalog->count; ++j)
                if (j != i && !xx_str_icmp(catalog->items[j].name, s.name)) unique = false;
            if (unique && s.plain_size <= RED_MAX_UNPACKED - aggregate) {
                aggregate += s.plain_size;
                xx_str_free(m->name);
                m->name = s.name;
                s.name = NULL;
                m->resolved_plain = s.plain;
                s.plain = NULL;
                m->unpacked_size = s.plain_size;
                m->crc = s.crc;
            }
        }
        xx_mem_free(s.plain);
        xx_str_free(s.name);
    }
    xx_str_free(directory);
    (void)xx_io_seek64(f->device, saved, SEEK_SET);
}
#endif
