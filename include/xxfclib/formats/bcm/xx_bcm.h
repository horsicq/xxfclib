/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_bcm.h @brief BCM compressed stream reader (identification only). */

#ifndef XXFCLIB_FORMAT_BCM_H
#define XXFCLIB_FORMAT_BCM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A BCM stream.
 *
 * BCM is Ilya Muravyov's context-mixing compressor. The container is four
 * magic bytes followed by an opaque arithmetic-coded bitstream with no
 * framing, no member name and no stored original size -- everything after the
 * magic only becomes meaningful once the CM model has been run.
 *
 * This reader therefore identifies and sizes a BCM stream but does not
 * decompress it, which is the same position XArchive takes: its XBCM is an
 * XExternalArchive that hands the payload to a separate backend rather than
 * decoding it in-tree. Enumerating a member that cannot be produced would be
 * worse than reporting none, so the archive-record count is zero.
 */
typedef struct xx_bcm {
    Abstractformat format;
    uint8_t version; /**< The digit of the "BCM<n>" magic. */
} xx_bcm;

typedef xx_bcm xx_bcm_t;
typedef xx_bcm XBcm;

XXFC_API void xx_bcm_init(xx_bcm *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_bcm *xx_bcm_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bcm_destroy(xx_bcm *archive);
XXFC_API void xx_bcm_free(xx_bcm *archive);

XXFC_API bool xx_bcm_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bcm_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bcm_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_bcm_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

/** @brief The format version digit, or 0 before the header is read. */
XXFC_API uint8_t xx_bcm_get_version(const xx_bcm *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BCM_H */
