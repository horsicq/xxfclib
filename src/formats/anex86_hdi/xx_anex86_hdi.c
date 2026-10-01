/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native raw hard-disk layout, independently implemented from:
 * https://github.com/aaru-dps/Aaru/tree/devel/Aaru.Images/Anex86
 */
#include "xxfclib/formats/anex86_hdi/xx_anex86_hdi.h"
#include "../wux/xx_disk_containers_native.h"

#ifdef ANEX86_HDI
#define DC_FILE_TYPE XX_FILE_TYPE_ANEX86_HDI
#else
#define DC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static bool dc_parse(Abstractformat *f, dc_image *image,
                      const xx_list_s *options, xx_pd_struct *pd) {
    uint8_t header[32];
    uint64_t header_size, declared_size, cylinders, heads, per_track, sector_size;
    uint64_t size;
    (void)options;
    if (!dc_read(f, image, 0U, header, sizeof(header), pd) || dc_le32(header)) return false;
    header_size = dc_le32(header + 8U);
    declared_size = dc_le32(header + 12U);
    sector_size = dc_le32(header + 16U);
    per_track = dc_le32(header + 20U);
    heads = dc_le32(header + 24U);
    cylinders = dc_le32(header + 28U);
    if (header_size < sizeof(header) || header_size > UINT32_C(1048576) ||
        !declared_size || !cylinders || !heads || !per_track ||
        sector_size < 128U || sector_size > 16384U || (sector_size & (sector_size - 1U))) return false;
    /* Divide the declared bounded length before multiplying untrusted values. */
    if (cylinders > declared_size / sector_size / heads / per_track) return false;
    size = cylinders * heads * per_track * sector_size;
    if (size != declared_size || !dc_span(header_size, size, image->available)) return false;
    image->extent = header_size + size;
    return dc_add(image, "disk.img", header_size, size, size, DC_STORED, 0U, header_size);
}

XX_DC_IMPLEMENT(anex86_hdi, DC_FILE_TYPE, "hdi", "application/x-anex86-hdi")
