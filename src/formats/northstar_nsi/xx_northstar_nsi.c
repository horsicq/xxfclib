/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native North Star NSI sector-image reader. Greaseweazle is used only as
 * an independent fixture producer; no code is imported from it.
 */
#include "xxfclib/formats/northstar_nsi/xx_northstar_nsi.h"
#include "../xx_payload_members.h"

#ifdef NORTHSTAR_NSI
#define NSI_FILE_TYPE XX_FILE_TYPE_NORTHSTAR_NSI
#else
#define NSI_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define NSI_CYLINDERS 35U
#define NSI_SECTORS 10U
#define NSI_FM_TRACK (NSI_SECTORS * 256U)
#define NSI_MFM_TRACK (NSI_SECTORS * 512U)
#define NSI_FM_SS (NSI_CYLINDERS * NSI_FM_TRACK)
#define NSI_MFM_SS (NSI_CYLINDERS * NSI_MFM_TRACK)
#define NSI_MFM_DS (2U * NSI_MFM_SS)

/* A single logical cylinder/head-interleaved image is returned. This makes
 * the side-out/reversed-side-1 wire order usable by standard disk readers.
 * The exact size is the sole format discriminator, hence named selection. */
static bool pm_parse(Abstractformat *format, pm_stream *stream,
                     xx_pd_struct *pd) {
    int64_t length = pm_available(format);
    uint8_t *logical;
    uint32_t track_size, heads, cylinder, head;
    uint64_t consumed = 0U;
    if (length != (int64_t)NSI_FM_SS &&
        length != (int64_t)NSI_MFM_SS &&
        length != (int64_t)NSI_MFM_DS)
        return false;
    if (pd && xx_pd_is_stopped(pd)) return false;
    track_size = length == (int64_t)NSI_FM_SS ? NSI_FM_TRACK : NSI_MFM_TRACK;
    heads = length == (int64_t)NSI_MFM_DS ? 2U : 1U;
    logical = (uint8_t *)xx_mem_alloc((size_t)length);
    if (!logical) return false;
    for (head = 0U; head < heads; ++head) {
        for (cylinder = 0U; cylinder < NSI_CYLINDERS; ++cylinder) {
            uint32_t physical_cylinder =
                head == 0U ? cylinder : NSI_CYLINDERS - 1U - cylinder;
            uint64_t source_at =
                ((uint64_t)head * NSI_CYLINDERS + cylinder) * track_size;
            uint64_t destination_at =
                ((uint64_t)physical_cylinder * heads + head) * track_size;
            if ((pd && xx_pd_is_stopped(pd)) ||
                !pm_read(format, (int64_t)source_at,
                         logical + (size_t)destination_at, track_size)) {
                xx_mem_free(logical);
                return false;
            }
            consumed += track_size;
        }
    }
    if (consumed != (uint64_t)length ||
        !pm_add(format, stream, "northstar.img", 0, 0)) {
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

void xx_northstar_nsi_init(xx_northstar_nsi *reader, xx_io_device *device,
                           int64_t base_address) {
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, NSI_FILE_TYPE, "nsi");
}

xx_northstar_nsi *xx_northstar_nsi_create(xx_io_device *device,
                                          int64_t base_address) {
    xx_northstar_nsi *reader =
        (xx_northstar_nsi *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_northstar_nsi_init(reader, device, base_address);
    return reader;
}

void xx_northstar_nsi_destroy(xx_northstar_nsi *reader) {
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_northstar_nsi_free(xx_northstar_nsi *reader) {
    if (reader) {
        xx_northstar_nsi_destroy(reader);
        xx_mem_free(reader);
    }
}
