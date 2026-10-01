/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native, bounded readers for three LibDsk-compatible disk layouts.
 */
#ifndef XX_LIBDSK_EXTRA_H
#define XX_LIBDSK_EXTRA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_myz80_s {
    Abstractformat format;
    uint32_t variant;
} xx_myz80;

typedef xx_myz80 xx_nanowasp;
typedef xx_myz80 xx_gotek;

/* MYZ80 has a 256-byte E5 header and up to 8 MiB of stored data. Missing
 * bytes reconstruct as E5, as the original driver's reads specify. The
 * no-magic layout should be selected by name. */
XXFC_API void xx_myz80_init(xx_myz80 *reader, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_myz80 *xx_myz80_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_myz80_destroy(xx_myz80 *reader);
XXFC_API void xx_myz80_free(xx_myz80 *reader);
XXFC_API bool xx_myz80_unpack_to_device(xx_myz80 *reader,
                                        xx_io_device *destination,
                                        xx_pd_struct *pd);

/* NanoWasp is an exactly 400 KiB, 40-cylinder, two-sided Microbee image.
 * Its physical side-out/sector-skew layout is returned as a logical CHS
 * image. No header exists; select the format by name. */
XXFC_API void xx_nanowasp_init(xx_nanowasp *reader, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_nanowasp *xx_nanowasp_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_nanowasp_destroy(xx_nanowasp *reader);
XXFC_API void xx_nanowasp_free(xx_nanowasp *reader);
XXFC_API bool xx_nanowasp_unpack_to_device(xx_nanowasp *reader,
                                           xx_io_device *destination,
                                           xx_pd_struct *pd);

/* Gotek USB collections use one fixed-size slot per disk: 1 MiB slots for
 * 720 KiB media, 1.5 MiB slots for 1.44 MiB media. density_kib must be 720
 * or 1440. The two headerless layouts require explicit named selection. */
XXFC_API void xx_gotek_init(xx_gotek *reader, xx_io_device *device,
                            int64_t base_address, unsigned density_kib);
XXFC_API xx_gotek *xx_gotek_create(xx_io_device *device,
                                   int64_t base_address,
                                   unsigned density_kib);
XXFC_API void xx_gotek_destroy(xx_gotek *reader);
XXFC_API void xx_gotek_free(xx_gotek *reader);
XXFC_API bool xx_gotek_unpack_disk_to_device(xx_gotek *reader, size_t slot,
                                             xx_io_device *destination,
                                             xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
#endif
