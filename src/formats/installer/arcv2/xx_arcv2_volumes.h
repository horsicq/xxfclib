/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Private, bounded ARCV2 two-piece member reconstruction. The terminal
 * descriptor supplies the combined packed length and JAMCRC. No filename
 * ordering is trusted: disk numbers, boundary names/metadata and checksums
 * must all agree. Ambiguous directories and incomplete sets remain failures. */
#include "../../../fs/platforms/xx_fs_platform.h"
#define ARCV2_VOLUME_LIMIT 32U
#define ARCV2_SIBLING_LIMIT 4096U
#define ARCV2_SET_LIMIT (INT64_C(512) * 1024 * 1024)

typedef struct arcv2_volume_scan_s {
    const char *directory, *selected_name;
    xx_pd_struct *pd;
    xx_io_device *devices[ARCV2_VOLUME_LIMIT];
    arcv2_stream *streams[ARCV2_VOLUME_LIMIT];
    unsigned seen, highest;
    int64_t bytes;
    bool selected_seen;
} arcv2_volume_scan;

static bool arcv2_volume_candidate(void *opaque, const char *name, bool directory)
{
    arcv2_volume_scan *scan = (arcv2_volume_scan *)opaque;
    uint8_t header[ARCV2_ARCHIVE_HEADER_SIZE];
    xx_io_device *device;
    Abstractformat logical;
    arcv2_stream *stream = NULL;
    char *path;
    unsigned number;
    int64_t size;
    if (++scan->seen > ARCV2_SIBLING_LIMIT || (scan->pd && xx_pd_is_stopped(scan->pd))) return false;
    if (directory) return true;
    if (xx_str_len(scan->directory) + xx_str_len(name) > 4094U) return false;
    path = xx_str_concat3(scan->directory, "/", name);
    if (!path) return false;
    device = xx_io_file_open(path, "rb");
    xx_str_free(path);
    if (!device) return true;
    if (!arcv2_read_at(device, 0, header, sizeof(header)) || xx_rt_memcmp(header, "ARCV\0\2\16\0", 8U) || xx_data_get_u32(header + 8U, 4, 0, false) == 1U) {
        xx_io_close(device);
        return true;
    }
    number = xx_data_get_u16(header + 12U, 2, 0, false);
    size = xx_io_total_size(device);
    xx_rt_memset(&logical, 0, sizeof(logical));
    logical.device = device;
    if (!number || number > ARCV2_VOLUME_LIMIT || scan->devices[number - 1U] || size <= 0 || size > ARCV2_SET_LIMIT - scan->bytes ||
        !arcv2_parse_single(&logical, &stream, scan->pd)) {
        xx_io_close(device);
        return false;
    }
    scan->devices[number - 1U] = device;
    scan->streams[number - 1U] = stream;
    scan->bytes += size;
    if (!strcmp(name, scan->selected_name)) scan->selected_seen = true;
    if (number > scan->highest) scan->highest = number;
    return true;
}

static void arcv2_put32(uint8_t *p, uint32_t value)
{
    unsigned i;
    for (i = 0; i < 4U; ++i) p[i] = (uint8_t)(value >> (8U * i));
}

static bool arcv2_volume_write(xx_io_device *out, const void *data, size_t size)
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

static bool arcv2_volume_copy(xx_io_device *out, xx_io_device *in, const arcv2_member *member, xx_crc_context *crc, xx_pd_struct *pd)
{
    uint8_t buffer[32768];
    uint32_t done = 0;
    while (done < member->data_size) {
        size_t take = member->data_size - done;
        if (take > sizeof(buffer)) take = sizeof(buffer);
        if ((pd && xx_pd_is_stopped(pd)) || !arcv2_read_at(in, member->data_offset + done, buffer, take) || !arcv2_volume_write(out, buffer, take)) return false;
        xx_crc_context_update(crc, buffer, take);
        done += (uint32_t)take;
    }
    return true;
}

static xx_io_device *arcv2_volume_view(Abstractformat *format, xx_pd_struct *pd)
{
    arcv2_volume_scan scan;
    const char *source, *cursor, *leaf;
    char *directory;
    size_t prefix = 0;
    unsigned i;
    xx_io_device *out = NULL;
    bool ok = false;
    static const uint8_t archive_header[14] = {'A', 'R', 'C', 'V', 0, 2, 14, 0, 1, 0, 0, 0, 1, 0};
    if (format->base_address || !(source = xx_io_source_path(format->device)) || xx_str_len(source) > 4094U || (pd && xx_pd_is_stopped(pd))) return NULL;
    for (cursor = source; *cursor; ++cursor)
        if (*cursor == '/' || *cursor == '\\') prefix = (size_t)(cursor - source) + 1U;
    leaf = source + prefix;
    directory = prefix ? xx_str_create_len(prefix) : xx_str_dup(".");
    if (!directory) return NULL;
    if (prefix) {
        xx_mem_copy(directory, source, prefix);
        directory[prefix] = 0;
    }
    xx_rt_memset(&scan, 0, sizeof(scan));
    scan.directory = directory;
    scan.selected_name = leaf;
    scan.pd = pd;
    if (!xx_fs_platform_enumerate(directory, arcv2_volume_candidate, &scan) || !scan.selected_seen || scan.highest < 2U) goto done;
    for (i = 0; i < scan.highest; ++i) {
        uint32_t flags = i == 0U ? 2U : i + 1U == scan.highest ? 8U : 4U;
        if (!scan.streams[i] || scan.streams[i]->volume_flags != flags) goto done;
    }
    out = xx_io_temp_open();
    if (!out || !arcv2_volume_write(out, archive_header, sizeof(archive_header))) goto done;
    for (i = 0; i < scan.highest; ++i) {
        arcv2_stream *stream = scan.streams[i];
        size_t j;
        for (j = 0; j < stream->count; ++j) {
            const arcv2_member *member = &stream->items[j], *tail = NULL;
            uint8_t header[ARCV2_BLOCK_FIXED_SIZE + UINT8_MAX], other[ARCV2_BLOCK_FIXED_SIZE + UINT8_MAX];
            uint32_t expected = member->packed_crc32;
            xx_crc_context crc;
            if (member->flags & ARCV2_FLAG_SPLIT_TAIL) {
                if (!i || j) goto done;
                continue; /* Already consumed together with the previous head. */
            }
            if (!arcv2_read_at(scan.devices[i], member->header_offset, header, (size_t)member->header_size)) goto done;
            if (member->flags & ARCV2_FLAG_SPLIT_HEAD) {
                size_t a = ARCV2_BLOCK_PREFIX_SIZE + header[16];
                uint64_t combined;
                if (j + 1U != stream->count || i + 1U >= scan.highest) goto done;
                tail = &scan.streams[i + 1U]->items[0];
                combined = (uint64_t)member->data_size + tail->data_size;
                if (!(tail->flags & ARCV2_FLAG_SPLIT_TAIL) || (member->flags & 0x30U) != (tail->flags & 0x30U) || strcmp(member->name, tail->name) ||
                    member->header_size != tail->header_size || combined != tail->compressed_size || combined > UINT32_MAX ||
                    !arcv2_read_at(scan.devices[i + 1U], tail->header_offset, other, (size_t)tail->header_size) || xx_rt_memcmp(header + a, other + a, 4U) ||
                    xx_rt_memcmp(header + a + 8U, other + a + 8U, 16U))
                    goto done;
                /* A head has no final size/checksum. Only the completed
                 * tail descriptor authorizes this synthesized whole record. */
                arcv2_put32(header + 8U, (member->flags & 0x30U) | ARCV2_FLAG_WHOLE);
                arcv2_put32(header + 12U, (uint32_t)combined);
                arcv2_put32(header + a + 4U, (uint32_t)combined);
                expected = tail->packed_crc32;
                arcv2_put32(header + a + 24U, expected);
            } else if (!(member->flags & ARCV2_FLAG_WHOLE)) goto done;
            if (!xx_crc_context_init_type(&crc, XX_CRC_TYPE_CRC32_JAMCRC) || !arcv2_volume_write(out, header, (size_t)member->header_size) ||
                !arcv2_volume_copy(out, scan.devices[i], member, &crc, pd) || (tail && !arcv2_volume_copy(out, scan.devices[i + 1U], tail, &crc, pd)) ||
                xx_crc_context_final(&crc) != expected)
                goto done;
        }
        /* Every continuation starts with the tail consumed above. */
        if (i && !(stream->items[0].flags & ARCV2_FLAG_SPLIT_TAIL)) goto done;
        if (i + 1U < scan.highest && !(stream->items[stream->count - 1U].flags & ARCV2_FLAG_SPLIT_HEAD)) goto done;
    }
    ok = xx_io_seek64(out, 0, SEEK_SET) == 0;
done:
    for (i = 0; i < ARCV2_VOLUME_LIMIT; ++i) {
        if (scan.streams[i]) arcv2_stream_free(scan.streams[i]);
        if (scan.devices[i]) xx_io_close(scan.devices[i]);
    }
    xx_str_free(directory);
    if (!ok && out) {
        xx_io_close(out);
        out = NULL;
    }
    return out;
}

static bool arcv2_parse(Abstractformat *format, arcv2_stream **result, xx_pd_struct *pd)
{
    int64_t saved;
    bool ok;
    xx_io_device *view;
    Abstractformat logical;
    if (!format || !format->device || !result || (saved = xx_io_tell(format->device)) < 0) return false;
    *result = NULL;
    ok = arcv2_parse_single(format, result, pd);
    if (ok && (*result)->volume_flags != 1U && (view = arcv2_volume_view(format, pd)) != NULL) {
        arcv2_stream *joined = NULL;
        logical = *format;
        logical.device = view;
        logical.base_address = 0;
        if (arcv2_parse_single(&logical, &joined, pd)) {
            joined->volumes = view;
            joined->archive_size = (*result)->archive_size;
            joined->volume_flags = (*result)->volume_flags;
            joined->disk_number = (*result)->disk_number;
            arcv2_stream_free(*result);
            *result = joined;
        } else xx_io_close(view);
    }
    if (xx_io_seek64(format->device, saved, SEEK_SET) != 0 || (pd && xx_pd_is_stopped(pd))) {
        if (ok) arcv2_stream_free(*result);
        *result = NULL;
        ok = false;
    }
    return ok;
}
