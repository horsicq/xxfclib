/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Bounded private reconstruction of complete Asymetrix disk sets. */
#include "../../../fs/platforms/xx_fs_platform.h"
#include <stdlib.h>
#define ASYM_VOLUME_LIMIT 256U
#define ASYM_SIBLING_LIMIT 4096U
#define ASYM_SET_LIMIT (INT64_C(512) * 1024 * 1024)
typedef struct asym_volume_scan_s {
    const char *directory, *selected_name;
    const uint8_t *identity;
    xx_pd_struct *pd;
    xx_io_device *devices[ASYM_VOLUME_LIMIT];
    int64_t sizes[ASYM_VOLUME_LIMIT], bytes;
    unsigned seen, highest;
    bool selected_seen;
} asym_volume_scan;
static bool asym_volume_candidate(void *opaque, const char *name, bool directory)
{
    asym_volume_scan *scan = (asym_volume_scan *)opaque;
    uint8_t header[44];
    xx_io_device *device;
    char *path;
    unsigned number;
    int64_t size;
    if (++scan->seen > ASYM_SIBLING_LIMIT || (scan->pd && xx_pd_is_stopped(scan->pd))) return false;
    if (directory) return true;
    if (xx_str_len(scan->directory) + xx_str_len(name) > 4094U) return false;
    path = xx_str_concat3(scan->directory, "/", name);
    if (!path) return false;
    device = xx_io_file_open(path, "rb");
    xx_str_free(path);
    if (!device) return true;
    if (!xx_io_read_at(device, 0, header, sizeof(header)) || xx_rt_memcmp(header, scan->identity, 40U) || xx_rt_memcmp(header + 42U, scan->identity + 42U, 2U)) {
        xx_io_close(device);
        return true;
    }
    number = xx_asymetrix_le16(header + 40U);
    size = xx_io_total_size(device);
    if (!number || number > ASYM_VOLUME_LIMIT || scan->devices[number - 1U] || size < 50 || size > ASYM_SET_LIMIT - scan->bytes) {
        xx_io_close(device);
        return false;
    }
    scan->devices[number - 1U] = device;
    scan->sizes[number - 1U] = size;
    scan->bytes += size;
    if (!xx_rt_strcmp(name, scan->selected_name)) scan->selected_seen = true;
    if (number > scan->highest) scan->highest = number;
    return true;
}
static void asym_volume_put32(uint8_t *p, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}
static bool asym_volume_write(xx_io_device *out, const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    while (size) {
        ssize_t n = xx_io_write(out, p, size);
        if (n <= 0 || (size_t)n > size) return false;
        p += n;
        size -= (size_t)n;
    }
    return true;
}
static xx_io_device *asym_volume_view(Abstractformat *self, xx_pd_struct *pd)
{
    asym_volume_scan scan;
    uint8_t header[44], block[6], buffer[32768];
    const char *source, *cursor;
    char *directory;
    uint8_t *table = NULL;
    size_t prefix = 0, table_size;
    unsigned i, count, used_highest = 1U, last_volume = 0;
    int64_t last_offset = 0;
    xx_io_device *out = NULL;
    bool ok = false;
    if (self->base_address || !(source = xx_io_source_path(self->device)) || xx_str_len(source) > 4094U || !xx_io_read_at(self->device, 0, header, sizeof(header)))
        return NULL;
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
    scan.identity = header;
    scan.pd = pd;
    if (!xx_fs_platform_enumerate(directory, asym_volume_candidate, &scan) || !scan.selected_seen || scan.highest < 2U) goto done;
    for (i = 0; i < scan.highest; ++i)
        if (!scan.devices[i]) goto done;
    count = xx_asymetrix_le16(header + 42U);
    table_size = (size_t)count * 108U;
    if (!count || count > XX_ASYMETRIX_MAX_MEMBERS || table_size > (uint64_t)(scan.sizes[0] - 44)) goto done;
    table = (uint8_t *)xx_mem_alloc(table_size);
    if (!table || !xx_io_read_at(scan.devices[0], 44, table, table_size)) goto done;
    out = xx_io_temp_open();
    header[40] = 1;
    header[41] = 0;
    if (!out || !asym_volume_write(out, header, sizeof(header)) || !asym_volume_write(out, table, table_size)) goto done;
    for (i = 0; i < count; ++i) {
        uint8_t *record = table + (size_t)i * 108U;
        uint32_t flags = xx_data_get_u32(record + 30U, 4, 0, false), raw = xx_data_get_u32(record + 38U, 4, 0, false);
        unsigned volume = flags >> 16U;
        int64_t offset = xx_data_get_u32(record + 34U, 4, 0, false), start = xx_io_tell(out);
        uint32_t produced = 0;
        if (start < 0 || start > UINT32_MAX || raw > XX_ASYMETRIX_MAX_DECODED || (pd && xx_pd_is_stopped(pd))) goto done;
        if (raw) {
            if (!volume || volume > scan.highest || offset < (volume == 1U ? 44 + (int64_t)table_size : 44)) goto done;
            if (!last_volume) {
                if (volume != 1U || offset != 44 + (int64_t)table_size) goto done;
            } else if (volume != last_volume || offset != last_offset) {
                /* A boundary between members can also have the terminal marker. */
                if (volume != last_volume + 1U || offset != 44 || last_offset + 6 != scan.sizes[last_volume - 1U] ||
                    !xx_io_read_at(scan.devices[last_volume - 1U], last_offset, block, 6U) || xx_asymetrix_le16(block) || xx_data_get_u32(block + 2U, 4, 0, false))
                    goto done;
            }
        }
        while (produced < raw) {
            uint32_t method, packed, wanted = raw - produced;
            size_t done = 0;
            if ((pd && xx_pd_is_stopped(pd)) || offset > scan.sizes[volume - 1U] - 6 || !xx_io_read_at(scan.devices[volume - 1U], offset, block, 6U)) goto done;
            method = xx_asymetrix_le16(block);
            packed = xx_data_get_u32(block + 2U, 4, 0, false);
            if (!method && !packed) {
                if (offset + 6 != scan.sizes[volume - 1U] || volume >= scan.highest) goto done;
                ++volume;
                offset = 44;
                continue;
            }
            if (wanted > 4096U) wanted = 4096U;
            if (method > 1U || !packed || packed > XX_ASYMETRIX_MAX_BLOCK_SIZE || (method == 0U && packed != wanted) ||
                packed > (uint64_t)(scan.sizes[volume - 1U] - offset - 6) || !asym_volume_write(out, block, 6U))
                goto done;
            while (done < packed) {
                size_t take = packed - done;
                if (take > sizeof(buffer)) take = sizeof(buffer);
                if (!xx_io_read_at(scan.devices[volume - 1U], offset + 6 + (int64_t)done, buffer, take) || !asym_volume_write(out, buffer, take)) goto done;
                done += take;
            }
            offset += 6 + packed;
            produced += wanted;
        }
        if (raw) {
            last_volume = volume;
            last_offset = offset;
            if (volume > used_highest) used_highest = volume;
        }
        asym_volume_put32(record + 30U, (flags & 0xffffU) | UINT32_C(0x10000));
        asym_volume_put32(record + 34U, (uint32_t)start);
    }
    if (used_highest != scan.highest || !last_volume || last_offset != scan.sizes[last_volume - 1U]) goto done;
    if (xx_io_seek64(out, 44, SEEK_SET) || !asym_volume_write(out, table, table_size) || xx_io_seek64(out, 0, SEEK_SET)) goto done;
    ok = true;
done:
    for (i = 0; i < ASYM_VOLUME_LIMIT; ++i)
        if (scan.devices[i]) xx_io_close(scan.devices[i]);
    xx_str_free(directory);
    xx_mem_free(table);
    if (!ok && out) {
        xx_io_close(out);
        out = NULL;
    }
    return out;
}
typedef struct asym_name_index_s {
    const char *name;
    size_t index;
} asym_name_index;
static int asym_name_order(const void *left, const void *right)
{
    const asym_name_index *a = (const asym_name_index *)left, *b = (const asym_name_index *)right;
    const char *x = a->name, *y = b->name;
    while (*x || *y) {
        unsigned char p = (unsigned char)*x, q = (unsigned char)*y;
        if (p >= 'a' && p <= 'z') p -= 32U;
        if (q >= 'a' && q <= 'z') q -= 32U;
        if (p != q) return p < q ? -1 : 1;
        ++x;
        ++y;
    }
    return 0;
}
static bool asym_unique_names(xx_asymetrix_stream *stream)
{
    asym_name_index *names;
    size_t i;
    names = (asym_name_index *)xx_mem_alloc(stream->count * sizeof(*names));
    if (!names) return false;
    for (i = 0; i < stream->count; ++i) {
        names[i].name = stream->items[i].name;
        names[i].index = i;
    }
    qsort(names, stream->count, sizeof(*names), asym_name_order);
    for (i = 0; i < stream->count;) {
        size_t end = i + 1U, first = names[i].index, j;
        while (end < stream->count && !asym_name_order(&names[i], &names[end])) {
            if (names[end].index < first) first = names[end].index;
            ++end;
        }
        for (j = i; j < end; ++j)
            if (names[j].index != first) {
                char renamed[80];
                char *owned;
                /* The suffix alone exceeds the 12-byte original-name limit,
                 * so no on-disk name can collide with this ordinal namespace. */
                if (snprintf(renamed, sizeof(renamed), "%s.__xxfc_dup_%u", names[j].name, (unsigned)names[j].index) < 0 || !(owned = xx_str_dup(renamed))) {
                    size_t k;
                    for (k = i; k < j; ++k)
                        if (names[k].index != first) {
                            xx_str_free(stream->items[names[k].index].name);
                            stream->items[names[k].index].name = (char *)names[k].name;
                        }
                    xx_mem_free(names);
                    return false;
                }
                stream->items[names[j].index].name = owned;
            }
        /* Comparison pointers must remain alive until this group is done. */
        for (j = i; j < end; ++j)
            if (names[j].index != first) xx_str_free((char *)names[j].name);
        i = end;
    }
    xx_mem_free(names);
    return true;
}
static xx_asymetrix_stream *xx_asymetrix_parse(Abstractformat *self, xx_pd_struct *pd)
{
    xx_asymetrix_stream *stream, *joined;
    xx_io_device *view;
    Abstractformat logical;
    int64_t saved;
    uint8_t header[44];
    if (!self || !self->device || (saved = xx_io_tell(self->device)) < 0) return NULL;
    stream = xx_asymetrix_parse_single(self, pd);
    if (stream && xx_io_read_at(self->device, self->base_address, header, sizeof(header)) &&
        (xx_asymetrix_le16(header + 40U) != 1U || stream->count < xx_asymetrix_le16(header + 42U)) && (view = asym_volume_view(self, pd)) != NULL) {
        logical = *self;
        logical.device = view;
        logical.base_address = 0;
        joined = xx_asymetrix_parse_single(&logical, pd);
        if (joined && joined->count == xx_asymetrix_le16(header + 42U)) {
            size_t i;
            joined->volumes = view;
            joined->archive_size = stream->archive_size;
            for (i = 0; i < joined->count; ++i) joined->items[i].check_crc = true;
            xx_asymetrix_stream_free(stream);
            stream = joined;
        } else {
            if (joined) xx_asymetrix_stream_free(joined);
            xx_io_close(view);
        }
    }
    if (stream && stream->count && !asym_unique_names(stream)) {
        xx_asymetrix_stream_free(stream);
        stream = NULL;
    }
    if (xx_io_seek64(self->device, saved, SEEK_SET) || (pd && xx_pd_is_stopped(pd))) {
        xx_asymetrix_stream_free(stream);
        stream = NULL;
    }
    return stream;
}
