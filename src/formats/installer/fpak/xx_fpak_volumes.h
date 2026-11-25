/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Private, conservative reconstruction of complete FPAK/FPAC volume pairs. */
#include "../../../fs/platforms/xx_fs_platform.h"
typedef struct fpak_volume_scan_s {
    const char *directory, *selected_name;
    const xx_fpak_stream *selected;
    xx_pd_struct *pd;
    xx_io_device *match;
    unsigned seen;
    int64_t bytes;
    bool chain, reverse;
    unsigned desired;
} fpak_volume_scan;

static bool fpak_volume_adjacent(const xx_fpak_stream *a, const xx_fpak_stream *b, bool pair)
{
    const xx_fpak_segment *x, *y;
    if (!a || !b || (pair && !a->is_lead) || b->is_lead || a->is_truncated || b->is_truncated || !a->segment_count || !b->segment_count) return false;
    x = &a->segments[a->segment_count - 1U];
    y = &b->segments[0];
    return x->data_size < x->packed_size && y->data_size < y->packed_size &&
           (pair ? x->data_size == x->packed_size - y->data_size : x->data_size <= x->packed_size - y->data_size) &&
           !xx_rt_memcmp(x->pinned_header + 4U, y->pinned_header + 4U, 20U) && xx_str_equals(x->name, y->name);
}
static int fpak_volume_number(const char *name)
{
    const char *dot = NULL, *p;
    for (p = name; *p; ++p)
        if (*p == '.') dot = p;
    if (!dot || xx_str_len(dot) != 4U || (dot[1] != 'P' && dot[1] != 'p') || (dot[2] != 'A' && dot[2] != 'a')) return -1;
    if (dot[3] == 'K' || dot[3] == 'k') return 0;
    return dot[3] >= '1' && dot[3] <= '9' ? dot[3] - '0' : -1;
}
static bool fpak_volume_candidate(void *opaque, const char *name, bool directory)
{
    fpak_volume_scan *scan = (fpak_volume_scan *)opaque;
    uint8_t signature[4];
    xx_io_device *device;
    xx_fpak_stream *other;
    Abstractformat logical;
    char *path;
    int64_t size;
    bool matches;
    if (++scan->seen > 4096U || (scan->pd && xx_pd_is_stopped(scan->pd))) return false;
    if (directory || !xx_rt_strcmp(name, scan->selected_name)) return true;
    if (scan->chain && fpak_volume_number(name) != (int)scan->desired) return true;
    if (xx_str_len(scan->directory) + xx_str_len(name) > 4094U) return false;
    path = xx_str_concat3(scan->directory, "/", name);
    if (!path) return false;
    device = xx_io_file_open(path, "rb");
    xx_str_free(path);
    if (!device) return true;
    if (!xx_io_read_at(device, 0, signature, sizeof(signature)) ||
        xx_rt_memcmp(signature, scan->chain ? (scan->desired ? "FPAC" : "FPAK") : (scan->selected->is_lead ? "FPAC" : "FPAK"), 4U)) {
        xx_io_close(device);
        return true;
    }
    size = xx_io_total_size(device);
    if (size < 4 || size > XX_FPAK_MAX_SIZE - scan->bytes) {
        xx_io_close(device);
        return false;
    }
    scan->bytes += size;
    other = (xx_fpak_stream *)xx_mem_calloc(1U, sizeof(*other));
    if (!other) {
        xx_io_close(device);
        return false;
    }
    xx_mem_zero(&logical, sizeof(logical));
    logical.device = device;
    matches = xx_fpak_read_volume(&logical, other, false, scan->pd) &&
              (scan->chain ? (scan->reverse ? fpak_volume_adjacent(other, scan->selected, false) : fpak_volume_adjacent(scan->selected, other, false))
                           : (scan->selected->is_lead ? fpak_volume_adjacent(scan->selected, other, true) : fpak_volume_adjacent(other, scan->selected, true)));
    xx_fpak_stream_free(other);
    if (matches && !scan->match) {
        scan->match = device;
        return true;
    }
    xx_io_close(device);
    return !matches; /* A second match makes the pairing ambiguous. */
}
static bool fpak_volume_copy(xx_io_device *out, xx_io_device *in, int64_t start, xx_pd_struct *pd)
{
    uint8_t buffer[32768];
    int64_t size = xx_io_total_size(in);
    if (start < 0 || size < start) return false;
    while (start < size) {
        size_t count = size - start < (int64_t)sizeof(buffer) ? (size_t)(size - start) : sizeof(buffer), done = 0;
        if ((pd && xx_pd_is_stopped(pd)) || !xx_io_read_at(in, start, buffer, count)) return false;
        while (done < count) {
            ssize_t n = xx_io_write(out, buffer + done, count - done);
            if (n <= 0 || (size_t)n > count - done) return false;
            done += (size_t)n;
        }
        start += (int64_t)count;
    }
    return true;
}
static xx_io_device *fpak_volume_view(Abstractformat *self, const xx_fpak_stream *selected, xx_pd_struct *pd)
{
    fpak_volume_scan scan;
    const char *source, *cursor;
    char *directory;
    size_t prefix = 0;
    xx_io_device *out = NULL;
    if (self->base_address || selected->is_truncated || !(source = xx_io_source_path(self->device)) || xx_str_len(source) > 4094U) return NULL;
    for (cursor = source; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') prefix = (size_t)(cursor - source) + 1U;
    directory = prefix ? xx_str_create_len(prefix) : xx_str_dup(".");
    if (!directory) return NULL;
    if (prefix) {
        xx_mem_copy(directory, source, prefix);
        directory[prefix] = 0;
    }
    xx_mem_zero(&scan, sizeof(scan));
    scan.directory = directory;
    scan.selected_name = source + prefix;
    scan.selected = selected;
    scan.pd = pd;
    scan.bytes = selected->volume_size;
    if (!xx_fs_platform_enumerate(directory, fpak_volume_candidate, &scan) || !scan.match) goto done;
    out = xx_io_temp_open();
    if (!out || !fpak_volume_copy(out, selected->is_lead ? self->device : scan.match, 0, pd) ||
        !fpak_volume_copy(out, selected->is_lead ? scan.match : self->device, 4, pd) || xx_io_seek64(out, 0, SEEK_SET)) {
        if (out) xx_io_close(out);
        out = NULL;
    }
done:
    if (scan.match) xx_io_close(scan.match);
    xx_str_free(directory);
    return out;
}
#include "xx_fpak_chain.h"
static xx_fpak_stream *xx_fpak_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_fpak_stream *stream, *joined;
    xx_io_device *view;
    Abstractformat logical;
    int64_t saved;
    size_t i;
    bool complete = true;
    if (!self || !self->device || (saved = xx_io_tell(self->device)) < 0) return NULL;
    stream = xx_fpak_parse_single(self, pd);
    if (stream)
        for (i = 0; i < stream->member_count; ++i)
            if (!stream->members[i].complete) complete = false;
    view = stream && !complete ? fpak_volume_view(self, stream, pd) : NULL;
    if (!view && stream && !complete) view = fpak_volume_chain_view(self, stream, pd);
    if (view) {
        logical = *self;
        logical.device = view;
        logical.base_address = 0;
        joined = xx_fpak_parse_single(&logical, pd);
        complete = joined && !joined->is_truncated;
        if (joined) {
            joined->volumes = view;
            for (i = 0; complete && i < joined->member_count; ++i) {
                uint8_t *plain = NULL;
                size_t size = 0;
                complete = joined->members[i].complete && xx_fpak_decode(&logical, joined, &joined->members[i], &plain, &size, pd);
                xx_mem_free(plain);
            }
            if (complete) {
                joined->parsed_size = stream->parsed_size;
                joined->volume_size = stream->volume_size;
                xx_fpak_stream_free(stream);
                stream = joined;
            } else xx_fpak_stream_free(joined);
        } else xx_io_close(view);
    }
    if (xx_io_seek64(self->device, saved, SEEK_SET) || (pd && xx_pd_is_stopped(pd))) {
        xx_fpak_stream_free(stream);
        stream = NULL;
    }
    return stream;
}
