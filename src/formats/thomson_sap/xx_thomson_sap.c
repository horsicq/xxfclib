/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Original native implementation of the producer's sector container format.
 * Primary reference (archive constants, do_write_sector and seek_pos):
 * https://github.com/jfdelnero/HxCFloppyEmulator/blob/main/libhxcfe/sources/thirdpartylibs/libsap/libsap.c
 * https://github.com/jfdelnero/HxCFloppyEmulator/blob/main/libhxcfe/sources/thirdpartylibs/libsap/libsap.h
 * No GPL implementation code is imported.
 */
#include "xxfclib/formats/thomson_sap/xx_thomson_sap.h"
#include "../wux/xx_disk_containers_native.h"

#ifdef THOMSON_SAP
#define DC_FILE_TYPE XX_FILE_TYPE_THOMSON_SAP
#else
#define DC_FILE_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static bool dc_parse(Abstractformat *f, dc_image *image,
                      const xx_list_s *options, xx_pd_struct *pd) {
    static const char signature[] = "SYSTEME D'ARCHIVAGE PUKALL S.A.P. (c) Alexandre PUKALL Avril 1998";
    uint8_t header[66], sector[256];
    uint32_t count, i;
    uint64_t physical;
    (void)options;
    if (!dc_read(f, image, 0U, header, sizeof(header), pd) ||
        (header[0] != 1U && header[0] != 2U) ||
        xx_rt_memcmp(header + 1U, signature, sizeof(signature) - 1U)) return false;
    image->block_size = header[0] == 1U ? 256U : 128U;
    count = header[0] == 1U ? 1280U : 640U;
    image->data_offset = sizeof(header);
    physical = (uint64_t)count * (image->block_size + 6U);
    if (!dc_span(image->data_offset, physical, image->available)) return false;
    for (i = 0U; i < count; ++i)
        if (dc_stopped(pd) || !dc_sap_sector(f, image, i, sector, pd)) return false;
    image->extent = image->data_offset + physical;
    return dc_add(image, "disk.img", image->data_offset,
                   (uint64_t)count * image->block_size, physical,
                   DC_SAP_SECTORS, 0U, sizeof(header));
}

XX_DC_IMPLEMENT(thomson_sap, DC_FILE_TYPE, "sap", "application/x-thomson-sap")
