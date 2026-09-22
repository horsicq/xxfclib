/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_freearc.h @brief FreeArc archive reader (identification only). */

#ifndef XXFCLIB_FORMAT_FREEARC_H
#define XXFCLIB_FORMAT_FREEARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A FreeArc archive.
 *
 * FreeArc stores its members behind a configurable chain of codecs named in
 * the archive's own directory block -- LZMA, PPMD, Tornado, GRZip and others,
 * in any order. There is no single codec to implement, so this reader
 * identifies the container and stops; XArchive's XFREEARC delegates to an
 * external backend for the same reason.
 */
typedef struct xx_freearc {
    Abstractformat format;
    uint16_t flags;   /**< Header flags, bytes 4..5. */
    uint16_t version; /**< Header version, bytes 6..7. */
} xx_freearc;

typedef xx_freearc xx_freearc_t;
typedef xx_freearc XFreearc;

XXFC_API void xx_freearc_init(xx_freearc *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_freearc *xx_freearc_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_freearc_destroy(xx_freearc *archive);
XXFC_API void xx_freearc_free(xx_freearc *archive);

XXFC_API bool xx_freearc_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_freearc_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_freearc_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_freearc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API uint16_t xx_freearc_get_flags(const xx_freearc *archive);
XXFC_API uint16_t xx_freearc_get_version(const xx_freearc *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_FREEARC_H */
