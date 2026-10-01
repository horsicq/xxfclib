/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native CMD FD2000/FD4000 head-unswapping reader. Greaseweazle is used
 * only as an independent fixture producer; no code is imported from it.
 */
#include "xxfclib/formats/cmd_fd/xx_cmd_fd.h"
#include "../xx_payload_members.h"

#define CMD_CYLINDERS 81U
#define CMD_HEADS 2U
#define CMD_TRACK_D1M (10U * 512U)
#define CMD_TRACK_D2M (20U * 512U)
#define CMD_TRACK_D4M (40U * 512U)

static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    xx_cmd_fd *reader = (xx_cmd_fd *)format;
    int64_t length = pm_available(format);
    uint8_t *logical;
    uint32_t track_size, cylinder, head;
    if (!reader || (reader->density != 1U && reader->density != 2U &&
                    reader->density != 4U)) return false;
    track_size = reader->density == 1U ? CMD_TRACK_D1M :
                 reader->density == 2U ? CMD_TRACK_D2M : CMD_TRACK_D4M;
    if (length != (int64_t)(CMD_CYLINDERS * CMD_HEADS * track_size) ||
        (pd && xx_pd_is_stopped(pd))) return false;
    logical = (uint8_t *)xx_mem_alloc((size_t)length);
    if (!logical) return false;
    for (cylinder = 0U; cylinder < CMD_CYLINDERS; ++cylinder) {
        for (head = 0U; head < CMD_HEADS; ++head) {
            uint64_t source_at =
                ((uint64_t)cylinder * CMD_HEADS + head) * track_size;
            uint64_t destination_at =
                ((uint64_t)cylinder * CMD_HEADS + (head ^ 1U)) * track_size;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !pm_read(format, (int64_t)source_at,
                         logical + (size_t)destination_at, track_size)) {
                xx_mem_free(logical);
                return false;
            }
        }
    }
    if (!pm_add(format, stream, "cmd.img", 0, 0)) {
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

void xx_cmd_fd_init(xx_cmd_fd *reader, xx_io_device *device,
                    int64_t base_address, unsigned density) {
    xx_file_type_t type = XX_FILE_TYPE_UNKNOWN;
    const char *extension = density == 1U ? "d1m" :
                            density == 2U ? "d2m" :
                            density == 4U ? "d4m" : "";
    if (!reader) return;
#ifdef CMD_FD
    type = density == 1U ? XX_FILE_TYPE_CMD_D1M :
           density == 2U ? XX_FILE_TYPE_CMD_D2M :
           density == 4U ? XX_FILE_TYPE_CMD_D4M : XX_FILE_TYPE_UNKNOWN;
#endif
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, type, extension);
    reader->density = density;
}

xx_cmd_fd *xx_cmd_fd_create(xx_io_device *device, int64_t base_address,
                            unsigned density) {
    xx_cmd_fd *reader = (xx_cmd_fd *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_cmd_fd_init(reader, device, base_address, density);
    return reader;
}

void xx_cmd_fd_destroy(xx_cmd_fd *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_cmd_fd_free(xx_cmd_fd *reader) {
    if (reader) {
        xx_cmd_fd_destroy(reader);
        xx_mem_free(reader);
    }
}
