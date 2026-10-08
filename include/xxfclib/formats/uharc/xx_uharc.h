/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_uharc.h @brief UHARC archive reader (identification only). */

#ifndef XXFCLIB_FORMAT_UHARC_H
#define XXFCLIB_FORMAT_UHARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A UHARC (.uha) archive by Uwe Herklotz.
 *
 * The file opens with "UHA" and a version byte whose high nibble is the
 * major and low nibble the minor version of the program that wrote it
 * (0x02 = 0.2, 0x04 = 0.4, 0x06 = 0.6). Everything after those four bytes,
 * including the member directory and member names, is entropy coded by
 * UHARC's closed-source ALZ/PPM/LZP coders -- even in STORE mode only the
 * member bytes are plain. No open specification or implementation of the
 * coder exists, so this reader identifies the container and advertises no
 * records.
 */
typedef struct xx_uharc {
    Abstractformat format;
    uint8_t version; /**< Raw version byte at offset 3. */
} xx_uharc;

typedef xx_uharc xx_uharc_t;
typedef xx_uharc XUharc;

XXFC_API void xx_uharc_init(xx_uharc *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_uharc *xx_uharc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_uharc_destroy(xx_uharc *archive);
XXFC_API void xx_uharc_free(xx_uharc *archive);

XXFC_API bool xx_uharc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_uharc_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_uharc_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_uharc_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

/** @brief Raw version byte (0x02, 0x04, 0x06, ...); 0 when not probed. */
XXFC_API uint8_t xx_uharc_get_version(const xx_uharc *archive);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_uharc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_uharc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_uharc_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_UHARC_H */
