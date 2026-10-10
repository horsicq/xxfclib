/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * PCE XDF logical-sector order, checked against original PCE writer/loader.
 */
#include "xxfclib/formats/pce_xdf/xx_pce_xdf.h"
#include "../xx_payload_members.h"

#ifdef PCE_XDF
#define XDF_FILE_TYPE XX_FILE_TYPE_PCE_XDF
#else
#define XDF_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

#define XDF_CYLINDERS 80U
#define XDF_TRACK_BYTES (46U * 512U)
#define XDF_IMAGE_BYTES (XDF_CYLINDERS * XDF_TRACK_BYTES)
#define XDF_LOGICAL_BYTES (38U * 512U + (XDF_CYLINDERS - 1U) * XDF_TRACK_BYTES)

typedef struct xdf_sector_s {
    uint8_t unit_offset, unit_length;
} xdf_sector;

/* Each array is in PCE's restored track/sector order. Values denote 512-byte
 * positions in the physical cylinder, so the reordered output is not a raw
 * copy of the source image. Cylinder zero has one 512-byte sector per entry. */
static const uint8_t xdf_cylinder_zero[38] = {12, 9,  0,  10, 1,  13, 2,  14, 3,  15, 4,  16, 5,  17, 6,  18, 7,  19, 8,
                                              42, 28, 43, 29, 44, 30, 45, 31, 11, 32, 23, 33, 24, 34, 25, 35, 26, 36, 27};
static const xdf_sector xdf_later_cylinder[8] = {{0, 2}, {22, 1}, {2, 4}, {24, 16}, {40, 4}, {23, 1}, {44, 2}, {6, 16}};

static bool pm_parse(Abstractformat *format, pm_stream *stream, xx_pd_struct *pd)
{
    uint8_t *logical;
    uint32_t cursor = 0U, cylinder, sector;
    if (pm_available(format) != XDF_IMAGE_BYTES || (pd && xx_pd_is_stopped(pd))) return false;
    logical = (uint8_t *)xx_mem_alloc(XDF_LOGICAL_BYTES);
    if (!logical) return false;
    for (sector = 0U; sector < 38U; ++sector) {
        if ((pd && xx_pd_is_stopped(pd)) || !pm_read(format, (int64_t)xdf_cylinder_zero[sector] * 512, logical + cursor, 512U)) goto fail;
        cursor += 512U;
    }
    for (cylinder = 1U; cylinder < XDF_CYLINDERS; ++cylinder) {
        for (sector = 0U; sector < 8U; ++sector) {
            uint32_t amount = (uint32_t)xdf_later_cylinder[sector].unit_length * 512U;
            int64_t at = (int64_t)cylinder * XDF_TRACK_BYTES + (int64_t)xdf_later_cylinder[sector].unit_offset * 512;
            if ((pd && xx_pd_is_stopped(pd)) || !pm_read(format, at, logical + cursor, amount)) goto fail;
            cursor += amount;
        }
    }
    if (cursor != XDF_LOGICAL_BYTES || !pm_add(format, stream, "xdf-pce-sector-order.img", 0, XDF_LOGICAL_BYTES)) goto fail;
    stream->items[stream->count - 1U].memory = logical;
    stream->size = XDF_IMAGE_BYTES;
    return true;
fail:
    xx_mem_free(logical);
    return false;
}

void xx_pce_xdf_init(xx_pce_xdf *reader, xx_io_device *device, int64_t base_address)
{
    if (!reader) return;
    xx_mem_zero(reader, sizeof(*reader));
    pm_init(&reader->format, device, base_address, XDF_FILE_TYPE, "xdf");
}

xx_pce_xdf *xx_pce_xdf_create(xx_io_device *device, int64_t base_address)
{
    xx_pce_xdf *reader = (xx_pce_xdf *)xx_mem_alloc(sizeof(*reader));
    if (reader) xx_pce_xdf_init(reader, device, base_address);
    return reader;
}

void xx_pce_xdf_destroy(xx_pce_xdf *reader)
{
    if (reader) xx_format_cleanup_extra_parameters(&reader->format);
}

void xx_pce_xdf_free(xx_pce_xdf *reader)
{
    if (reader) {
        xx_pce_xdf_destroy(reader);
        xx_mem_free(reader);
    }
}
