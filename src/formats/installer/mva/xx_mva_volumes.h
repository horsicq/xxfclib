/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Private, authenticated two-volume MVA/MVB view. No filename-prefix guesses. */
#include "../../../fs/platforms/xx_fs_platform.h"
#define MVA_VOLUME_LIMIT (INT64_C(256) * 1024 * 1024)
typedef struct mva_volume_scan_s {
    Abstractformat *format;
    const char *source;
    const char *directory;
    xx_pd_struct *pd;
    xx_mva_stream *result;
    unsigned seen, candidates;
    int selected;
    bool ambiguous;
} mva_volume_scan;
static int mva_volume_letter(const char *name)
{
    size_t n = xx_str_len(name);
    unsigned char a, b, c;
    if (n < 5 || name[n - 4] != '.') return -1;
    a = (unsigned char)name[n - 3];
    b = (unsigned char)name[n - 2];
    c = (unsigned char)name[n - 1];
    if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
    if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return a == 'm' && b == 'v' && (c == 'a' || c == 'b') ? c - 'a' : -1;
}
static bool mva_join_authenticated(Abstractformat *logical, xx_mva_stream *stream, int64_t boundary, xx_pd_struct *pd)
{
    int64_t size = xx_io_total_size(logical->device);
    uint64_t budget = 0;
    bool crossing = false;
    if (stream->incomplete || stream->archive_size != size) return false;
    for (size_t i = 0; i < stream->count; ++i) {
        const xx_mva_member *m = &stream->items[i];
        uint8_t *packed = NULL, *plain = NULL;
        xx_io_device *sink = NULL;
        size_t consumed = 0;
        bool valid = false;
        if (pd && xx_pd_is_stopped(pd)) return false;
        budget += (uint64_t)m->uncompressed_size + (uint64_t)m->compressed_size;
        if (budget > MVA_VOLUME_LIMIT) return false;
        if (m->method == XX_MVA_METHOD_STORE) continue;
        if (m->compressed_size < 8 || m->uncompressed_size <= 0) return false;
        packed = (uint8_t *)xx_mem_alloc((size_t)m->compressed_size);
        plain = (uint8_t *)xx_mem_alloc((size_t)m->uncompressed_size);
        if (packed && plain && xx_mva_read_at(logical, m->data_offset, packed, (size_t)m->compressed_size) &&
            xx_zlib_stream_header_is_valid(packed, (size_t)m->compressed_size)) {
            sink = xx_io_mem_open(plain, (size_t)m->uncompressed_size);
            valid = sink && xx_deflate_unpack_memory_to_device_ex(packed + 2, (size_t)m->compressed_size - 6U, sink, &consumed, false, pd) &&
                    consumed == (size_t)m->compressed_size - 6U && xx_io_tell(sink) == m->uncompressed_size &&
                    xx_zlib_stream_trailer_matches(packed, (size_t)m->compressed_size, plain, (size_t)m->uncompressed_size);
        }
        if (sink) xx_io_close(sink);
        xx_mem_free(plain);
        xx_mem_free(packed);
        if (!valid) return false;
        if (m->data_offset < boundary && m->compressed_size > boundary - m->data_offset) crossing = true;
    }
    return crossing && !(pd && xx_pd_is_stopped(pd));
}
static xx_mva_stream *mva_volume_trial(mva_volume_scan *scan, const char *candidate)
{
    xx_io_device *devices[2] = {NULL, NULL}, *view = NULL;
    xx_io_volume ranges[2];
    xx_mva_stream *stream = NULL;
    Abstractformat logical = *scan->format;
    uint8_t h[8];
    int64_t total = 0;
    for (unsigned i = 0; i < 2; ++i) {
        const char *path = (int)i == scan->selected ? scan->source : candidate;
        int64_t size;
        devices[i] = xx_io_file_open(path, "rb");
        if (!devices[i] || xx_io_read(devices[i], h, sizeof(h)) != sizeof(h) || xx_rt_memcmp(h, "mflh\1\0\0\0", 8)) goto done;
        size = xx_io_total_size(devices[i]);
        if (size <= 8 || size > MVA_VOLUME_LIMIT - total) goto done;
        ranges[i].device = devices[i];
        ranges[i].offset = i ? 8 : 0;
        ranges[i].size = size - (i ? 8 : 0);
        total += ranges[i].size;
    }
    view = xx_io_multivolume_open(ranges, 2, true);
    if (!view) goto done;
    devices[0] = devices[1] = NULL;
    logical.device = view;
    logical.base_address = 0;
    stream = xx_mva_parse_single(&logical, scan->pd);
    if (!stream || !mva_join_authenticated(&logical, stream, ranges[0].size, scan->pd)) {
        xx_mva_stream_free(stream);
        stream = NULL;
        goto done;
    }
    stream->volumes = view;
    view = NULL;
    stream->archive_size = xx_io_total_size(scan->format->device);
done:
    if (view) xx_io_close(view);
    for (unsigned i = 0; i < 2; ++i)
        if (devices[i]) xx_io_close(devices[i]);
    return stream;
}
static bool mva_volume_candidate(void *opaque, const char *name, bool directory)
{
    mva_volume_scan *scan = (mva_volume_scan *)opaque;
    char *path;
    xx_mva_stream *stream;
    if (++scan->seen > 4096 || (scan->pd && xx_pd_is_stopped(scan->pd))) return false;
    if (directory || mva_volume_letter(name) != 1 - scan->selected) return true;
    if (++scan->candidates > 64 || xx_str_len(scan->directory) + xx_str_len(name) > 4094) return false;
    path = xx_str_concat3(scan->directory, "/", name);
    if (!path) return false;
    stream = mva_volume_trial(scan, path);
    xx_str_free(path);
    if (!stream) return true;
    if (scan->result) {
        xx_mva_stream_free(stream);
        xx_mva_stream_free(scan->result);
        scan->result = NULL;
        scan->ambiguous = true;
        return false;
    }
    scan->result = stream;
    return true;
}
static xx_mva_stream *xx_mva_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_mva_stream *single = xx_mva_parse_single(self, pd);
    mva_volume_scan scan;
    const char *source, *at;
    char *directory;
    size_t prefix = 0;
    int letter;
    if ((single && !single->incomplete) || !self || self->base_address != 0 || (pd && xx_pd_is_stopped(pd)) || !(source = xx_io_source_path(self->device)) ||
        xx_str_len(source) > 4094 || (letter = mva_volume_letter(source)) < 0)
        return single;
    for (at = source; *at; ++at)
        if (*at == '/' || *at == '\\') prefix = (size_t)(at - source) + 1U;
    directory = prefix ? xx_str_create_len(prefix) : xx_str_dup(".");
    if (!directory) return single;
    if (prefix) {
        xx_mem_copy(directory, source, prefix);
        directory[prefix] = 0;
    }
    xx_mem_zero(&scan, sizeof(scan));
    scan.format = self;
    scan.source = source;
    scan.directory = directory;
    scan.pd = pd;
    scan.selected = letter;
    if (!xx_fs_platform_enumerate(directory, mva_volume_candidate, &scan) || scan.ambiguous) {
        xx_mva_stream_free(scan.result);
        scan.result = NULL;
    }
    xx_str_free(directory);
    if (scan.result) {
        xx_mva_stream_free(single);
        return scan.result;
    }
    if (scan.ambiguous) {
        xx_mva_stream_free(single);
        return NULL;
    }
    return single;
}
