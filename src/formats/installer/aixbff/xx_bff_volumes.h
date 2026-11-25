/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
/* Private file-backed AIX continuation-volume view. */
#include "../../../fs/platforms/xx_fs_platform.h"
#define BFF_VOLUME_LIMIT 256U
#define BFF_SIBLING_LIMIT 4096U
#define BFF_SET_SIZE_LIMIT (INT64_C(512) * 1024 * 1024)
typedef struct bff_volume_scan_s {
    const char *directory;
    const uint8_t *header;
    xx_pd_struct *pd;
    xx_io_device *devices[BFF_VOLUME_LIMIT];
    unsigned seen, highest;
} bff_volume_scan;
static bool bff_volume_candidate(void *opaque, const char *name, bool directory)
{
    bff_volume_scan *scan = (bff_volume_scan *)opaque;
    uint8_t header[XX_BFF_VOLUME_HEADER_SIZE];
    xx_io_device *device;
    char *path;
    unsigned number;
    if (++scan->seen > BFF_SIBLING_LIMIT || (scan->pd && xx_pd_is_stopped(scan->pd))) return false;
    if (directory) return true;
    if (xx_str_len(scan->directory) + xx_str_len(name) > 4094U) return false;
    path = xx_str_concat3(scan->directory, "/", name);
    if (!path) return false;
    device = xx_io_file_open(path, "rb");
    xx_str_free(path);
    if (!device) return true;
    if (!xx_bff_read(device, 0, header, sizeof(header)) || xx_data_get_u32(header, 4, 0, false) != XX_BFF_VOLUME_MAGIC || xx_rt_memcmp(header + 8, scan->header + 8, 8) ||
        xx_rt_memcmp(header + 20, scan->header + 20, 52)) {
        xx_io_close(device);
        return true;
    }
    number = xx_data_get_u16(header + 6, 2, 0, false);
    if (!number || number > BFF_VOLUME_LIMIT || scan->devices[number - 1U]) {
        xx_io_close(device);
        return false;
    }
    scan->devices[number - 1U] = device;
    if (number > scan->highest) scan->highest = number;
    return true;
}
static xx_io_device *bff_volume_view(Abstractformat *format, xx_pd_struct *pd)
{
    bff_volume_scan scan;
    xx_io_volume ranges[BFF_VOLUME_LIMIT];
    xx_io_device *view = NULL;
    const char *source, *cursor;
    char *directory = NULL;
    size_t prefix = 0U;
    uint8_t header[XX_BFF_VOLUME_HEADER_SIZE];
    int64_t combined = 0;
    unsigned i, selected;
    if (format->base_address != 0 || (pd && xx_pd_is_stopped(pd)) || !(source = xx_io_source_path(format->device)) || xx_str_len(source) > 4094U ||
        !xx_bff_read(format->device, 0, header, sizeof(header)) || xx_data_get_u32(header, 4, 0, false) != XX_BFF_VOLUME_MAGIC)
        return NULL;
    selected = xx_data_get_u16(header + 6, 2, 0, false);
    if (!selected || selected > BFF_VOLUME_LIMIT) return NULL;
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
    scan.header = header;
    scan.pd = pd;
    if (!xx_fs_platform_enumerate(directory, bff_volume_candidate, &scan) || scan.highest < 2U || !scan.devices[selected - 1U]) goto done;
    for (i = 0U; i < scan.highest; ++i) {
        int64_t size, start = i ? XX_BFF_VOLUME_HEADER_SIZE : 0;
        if (!scan.devices[i] || (pd && xx_pd_is_stopped(pd))) goto done;
        size = xx_io_total_size(scan.devices[i]);
        if (size <= start || size - start > BFF_SET_SIZE_LIMIT - combined) goto done;
        ranges[i].device = scan.devices[i];
        ranges[i].offset = start;
        ranges[i].size = size - start;
        combined += size - start;
    }
    view = xx_io_multivolume_open(ranges, scan.highest, true);
    if (view) xx_mem_zero(scan.devices, sizeof(scan.devices));
done:
    for (i = 0U; i < BFF_VOLUME_LIMIT; ++i)
        if (scan.devices[i]) xx_io_close(scan.devices[i]);
    xx_str_free(directory);
    return view;
}
static bool xx_bff_parse(Abstractformat *format, xx_bff_stream **result, xx_pd_struct *pd)
{
    int64_t saved;
    bool ok;
    xx_io_device *view;
    Abstractformat logical;
    if (!format || !format->device || !result || (pd && xx_pd_is_stopped(pd)) || (saved = xx_io_tell(format->device)) < 0) return false;
    *result = NULL;
    ok = xx_bff_parse_single(format, result, pd);
    if (!ok && (view = bff_volume_view(format, pd)) != NULL) {
        logical = *format;
        logical.device = view;
        logical.base_address = 0;
        ok = xx_bff_parse_single(&logical, result, pd);
        if (ok) {
            (*result)->volumes = view;
            /* Format size describes the selected physical input; member
             * offsets describe the reconstructed stream owned by the state. */
            (*result)->archive_end = xx_io_total_size(format->device);
        } else xx_io_close(view);
    }
    if (xx_io_seek64(format->device, saved, SEEK_SET) != 0) {
        if (ok) xx_bff_stream_free(*result);
        *result = NULL;
        ok = false;
    }
    return ok;
}
