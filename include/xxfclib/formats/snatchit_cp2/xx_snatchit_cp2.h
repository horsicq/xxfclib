/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_SNATCHIT_CP2_H
#define XX_SNATCHIT_CP2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Native Snatch-it CP2 v3.02 reader. Supported complete 512-byte sector
 * geometries: 160, 180, 320, 360, 720, 1200 and 1440 KiB. Healthy tracks
 * are reconstructed into one disk.img member. Incomplete/protected/error
 * sectors, duplicate IDs and unknown geometries are rejected. */
typedef struct xx_snatchit_cp2_s {
    Abstractformat format;
} xx_snatchit_cp2;
XXFC_API void xx_snatchit_cp2_init(xx_snatchit_cp2 *reader,
                                   xx_io_device *device,int64_t base_address);
XXFC_API xx_snatchit_cp2 *xx_snatchit_cp2_create(xx_io_device *device,
                                                  int64_t base_address);
XXFC_API void xx_snatchit_cp2_destroy(xx_snatchit_cp2 *reader);
XXFC_API void xx_snatchit_cp2_free(xx_snatchit_cp2 *reader);
XXFC_API bool xx_snatchit_cp2_unpack_to_device(xx_snatchit_cp2 *reader,
                                                xx_io_device *destination,
                                                xx_pd_struct *pd);
#ifdef __cplusplus
}
#endif
#endif
