/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SIMH_DISK_H
#define XX_SIMH_DISK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

/* The LibDsk SIMH CP/M driver stores 127x2x32 sectors with 128 data bytes
 * at offset +3 of each 137-byte physical record. This reader handles full
 * images only. The layout has no magic and requires explicit selection. */
typedef struct xx_simh_disk_s {
    Abstractformat format;
} xx_simh_disk;
XXFC_API void xx_simh_disk_init(xx_simh_disk *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_simh_disk *xx_simh_disk_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_simh_disk_destroy(xx_simh_disk *reader);
XXFC_API void xx_simh_disk_free(xx_simh_disk *reader);
XXFC_API bool xx_simh_disk_unpack_to_device(xx_simh_disk *reader, xx_io_device *destination, xx_pd_struct *pd);
#ifdef __cplusplus
}
#endif
#endif
