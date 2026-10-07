/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * T98-Next producer specification (LED, 2001-01-22):
 * https://www.pc98.org/project/doc/nhd.html
 * https://github.com/aaru-dps/Aaru/tree/devel/Aaru.Images/NHDr0
 */
#include "xxfclib/formats/nhd/xx_nhd.h"
#include "../wux/xx_disk_containers_native.h"
#include "xxfclib/data/xx_data.h"

#ifdef NHD
#define DC_FILE_TYPE XX_FILE_TYPE_NHD
#else
#define DC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static bool dc_parse(Abstractformat *f, dc_image *image,
                      const xx_list_s *options, xx_pd_struct *pd) {
    uint8_t header[512];
    uint64_t header_size, sectors, size;
    uint32_t cylinders, heads, per_track, sector_size;
    (void)options;
    if (!dc_read(f, image, 0U, header, sizeof(header), pd) ||
        xx_rt_memcmp(header, "T98HDDIMAGE.R0\0", 15U) || header[15U] ||
        !dc_zero(header + 286U, 226U)) return false;
    header_size = xx_data_get_u32(header + 272U, 4, 0, false);
    cylinders = xx_data_get_u32(header + 276U, 4, 0, false);
    heads = xx_data_get_u16(header + 280U, 2, 0, false);
    per_track = xx_data_get_u16(header + 282U, 2, 0, false);
    sector_size = xx_data_get_u16(header + 284U, 2, 0, false);
    if (header_size < sizeof(header) || header_size > UINT32_C(1048576) ||
        !cylinders || !heads || !per_track || sector_size < 128U || sector_size > 16384U ||
        (sector_size & (sector_size - 1U))) return false;
    sectors = (uint64_t)cylinders * heads;
    if (sectors > DC_MAX_IMAGE_SIZE / per_track / sector_size) return false;
    size = sectors * per_track * sector_size;
    if (!dc_span(header_size, size, image->available)) return false;
    image->extent = header_size + size;
    return dc_add(image, "disk.img", header_size, size, size, DC_STORED, 0U, header_size);
}

XX_DC_IMPLEMENT(nhd, DC_FILE_TYPE, "nhd", "application/x-nhd")
