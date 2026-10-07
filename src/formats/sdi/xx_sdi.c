/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native stored-section SDI reader. Layout references:
 * https://github.com/DiscUtils/DiscUtils/tree/develop/Library/DiscUtils.Sdi
 * Microsoft-produced C:\Windows\Boot\DVD\PCAT\boot.sdi verifies this layout.
 */
#include "xxfclib/formats/sdi/xx_sdi.h"
#include "../wux/xx_disk_containers_native.h"
#include "xxfclib/data/xx_data.h"

#ifdef SDI
#define DC_FILE_TYPE XX_FILE_TYPE_SDI
#else
#define DC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static bool dc_parse(Abstractformat *f, dc_image *image,
                      const xx_list_s *options, xx_pd_struct *pd) {
    uint8_t header[512], entry[64];
    uint64_t page, at, boot_at, boot_size;
    unsigned checksum = 0U;
    size_t i;
    bool terminated = false;
    (void)options;
    if (!dc_read(f, image, 0U, header, sizeof(header), pd) ||
        xx_rt_memcmp(header, "$SDI0001", 8U)) return false;
    for (i = 0U; i < sizeof(header); ++i) checksum += header[i];
    if ((checksum & 255U) != 0U) return false;
    page = xx_data_get_u64(header + 0x70U, 8, 0, false);
    if (!page || page > 2048U) return false;
    page *= 512U;
    if (!dc_span(page, page, image->available)) return false;
    boot_at = xx_data_get_u64(header + 0x10U, 8, 0, false);
    boot_size = xx_data_get_u64(header + 0x18U, 8, 0, false);
    if ((boot_at == 0U) != (boot_size == 0U) ||
        (boot_size && (!dc_span(boot_at, boot_size, image->available) || boot_at < 2U * page)))
        return false;
    for (at = page; at + sizeof(entry) <= 2U * page; at += sizeof(entry)) {
        char type[9], name[64];
        uint64_t offset, size;
        size_t length = 0U, j;
        if (!dc_read(f, image, at, entry, sizeof(entry), pd)) return false;
        if (dc_zero(entry, 8U)) { terminated = true; break; }
        while (length < 8U && entry[length]) {
            uint8_t c = entry[length];
            if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == ' '))
                return false;
            type[length] = (char)c;
            ++length;
        }
        if (!dc_zero(entry + length, 8U - length) || !length) return false;
        while (length && type[length - 1U] == ' ') --length;
        if (!length || xx_data_get_u64(entry + 8U, 8, 0, false) != 0U || !dc_zero(entry + 40U, 24U)) return false;
        type[length] = '\0';
        offset = xx_data_get_u64(entry + 16U, 8, 0, false);
        size = xx_data_get_u64(entry + 24U, 8, 0, false);
        if (offset < 2U * page || !dc_span(offset, size, image->available) || size > DC_MAX_IMAGE_SIZE)
            return false;
        for (j = 0U; j < image->count; ++j) {
            const dc_member *prior = &image->members[j];
            if (size && prior->size && offset < prior->offset + prior->size &&
                prior->offset < offset + size) return false;
        }
        xx_rt_snprintf(name, sizeof(name), "%03u-%s.%s", (unsigned)image->count,
                        type, !xx_rt_strcmp(type, "PART") || !xx_rt_strcmp(type, "DISK") ? "img" : "bin");
        if (!dc_add(image, name, offset, size, size, DC_STORED, at, sizeof(entry))) return false;
    }
    if (!terminated || !image->count) return false;
    /* SDI allows page padding and empty WIM placeholders at physical EOF. */
    image->extent = image->available;
    return true;
}

XX_DC_IMPLEMENT(sdi, DC_FILE_TYPE, "sdi", "application/x-sdi")
