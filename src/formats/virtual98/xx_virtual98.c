/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout and lazy-allocation behavior:
 * https://github.com/aaru-dps/Aaru/tree/devel/Aaru.Images/Virtual98
 * The producer writes raw sectors at 0xDC and leaves an unwritten suffix absent.
 */
#include "xxfclib/formats/virtual98/xx_virtual98.h"
#include "../wux/xx_disk_containers_native.h"
#include "xxfclib/data/xx_data.h"

#ifdef VIRTUAL98
#define DC_FILE_TYPE XX_FILE_TYPE_VIRTUAL98
#else
#define DC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static bool dc_parse(Abstractformat *f, dc_image *image, const xx_list_s *options, xx_pd_struct *pd)
{
    uint8_t header[220];
    uint64_t size, stored;
    uint32_t sector_size, sectors;
    (void)options;
    if (!dc_read(f, image, 0U, header, sizeof(header), pd) || xx_rt_memcmp(header, "VHD1.00\0", 8U)) return false;
    sector_size = xx_data_get_u16(header + 142U, 2, 0, false);
    sectors = xx_data_get_u32(header + 148U, 4, 0, false);
    if (!sectors || sector_size < 128U || sector_size > 16384U || (sector_size & (sector_size - 1U))) return false;
    size = (uint64_t)sectors * sector_size;
    stored = image->available - sizeof(header);
    if (size > DC_MAX_IMAGE_SIZE || stored > size || stored % sector_size) return false;
    image->extent = image->available;
    return dc_add(image, "disk.img", sizeof(header), size, stored, DC_LAZY_TAIL, 0U, sizeof(header));
}

XX_DC_IMPLEMENT(virtual98, DC_FILE_TYPE, "vhd", "application/x-virtual98")
