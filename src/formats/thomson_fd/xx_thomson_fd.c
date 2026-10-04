/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native Thomson FD side-sequential reader. Greaseweazle is used only as an
 * independent fixture producer; no code is imported from it.
 */
#include "xxfclib/formats/thomson_fd/xx_thomson_fd.h"
#include "../xx_payload_members.h"

#ifdef THOMSON_FD
#define THOMSON_FD_FILE_TYPE XX_FILE_TYPE_THOMSON_FD
#else
#define THOMSON_FD_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define FD_HEADS 2U
#define FD_SECTORS 16U
#define FD_SECTOR_SIZE 256U
#define FD_TRACK_SIZE (FD_SECTORS * FD_SECTOR_SIZE)
#define FD_2S160_CYLINDERS 40U
#define FD_2S320_CYLINDERS 80U
#define FD_2S160_BYTES (FD_2S160_CYLINDERS * FD_HEADS * FD_TRACK_SIZE)
#define FD_2S320_BYTES (FD_2S320_CYLINDERS * FD_HEADS * FD_TRACK_SIZE)

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    int64_t length = pm_available(format);
    uint8_t *logical;
    uint32_t cylinders, heads = FD_HEADS, head, cylinder;
    if (length != (int64_t)FD_2S160_BYTES &&
        length != (int64_t)FD_2S320_BYTES)
        return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    cylinders = length == (int64_t)FD_2S160_BYTES ?
                    FD_2S160_CYLINDERS : FD_2S320_CYLINDERS;
    if (((xx_thomson_fd *)format)->hxc_geometry) {
        cylinders = FD_2S320_CYLINDERS;
        heads = length == (int64_t)FD_2S160_BYTES ? 1U : FD_HEADS;
    }
    logical = (uint8_t *)xx_mem_alloc((size_t)length);
    if (!logical) return false;
    for (head = 0U; head < heads; ++head) {
        for (cylinder = 0U; cylinder < cylinders; ++cylinder) {
            uint64_t source_at =
                ((uint64_t)head * cylinders + cylinder) * FD_TRACK_SIZE;
            uint64_t destination_at =
                ((uint64_t)cylinder * heads + head) * FD_TRACK_SIZE;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !pm_read(format, (int64_t)source_at,
                         logical + (size_t)destination_at, FD_TRACK_SIZE)) {
                xx_mem_free(logical);
                return false;
            }
        }
    }
    if (!pm_add(format, stream, "thomson.img", 0, 0)) {
        xx_mem_free(logical);
        return false;
    }
    stream->items[stream->count - 1U].memory = logical;
    stream->items[stream->count - 1U].size = length;
    stream->items[stream->count - 1U].packed_size = length;
    stream->items[stream->count - 1U].offset = -1;
    stream->size = length;
    return true;
}

void xx_thomson_fd_init(xx_thomson_fd *reader, xx_io_device *device,
                        int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, THOMSON_FD_FILE_TYPE, "fd");
}

xx_thomson_fd *xx_thomson_fd_create(xx_io_device *device,
                                    int64_t base_address) {
    xx_thomson_fd *reader = (xx_thomson_fd *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_thomson_fd_init(reader, device, base_address);
    return reader;
}

void xx_thomson_fd_init_hxc(xx_thomson_fd *reader, xx_io_device *device,
                           int64_t base_address) {
    xx_thomson_fd_init(reader, device, base_address);
    if (reader) reader->hxc_geometry = true;
}

xx_thomson_fd *xx_thomson_fd_create_hxc(xx_io_device *device,
                                      int64_t base_address) {
    xx_thomson_fd *reader = (xx_thomson_fd *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_thomson_fd_init_hxc(reader, device, base_address);
    return reader;
}

void xx_thomson_fd_destroy(xx_thomson_fd *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_thomson_fd_free(xx_thomson_fd *reader) {
    if (reader) {
        xx_thomson_fd_destroy(reader);
        xx_mem_free(reader);
    }
}
