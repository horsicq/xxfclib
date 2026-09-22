/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_packimg.h @brief PACKIMG ("--PaCkImGs--") firmware tag reader. */

/* PACKIMG is the thinnest wrapper of the five router containers handled here.
 * A twelve byte ASCII tag, a couple of words, and then the payload - usually a
 * squashfs or an LZMA kernel that another reader in this library decodes.  It
 * appears in SerComm-derived firmware and is carried by several TRENDnet and
 * Edimax releases, normally as an inner blob rather than at offset zero, so
 * the reader is written to work from any base address.
 *
 *   header (32 bytes)
 *     +0x00  "--PaCkImGs--", twelve ASCII bytes, no terminator
 *     +0x0C  u32  unknown; zero in every sample inspected
 *     +0x10  u32  payload size, LITTLE endian
 *     +0x14  twelve bytes of padding, zero in every sample inspected
 *     +0x20  the payload
 *
 * Source: binwalk's src/signatures/packimg.rs for the magic and
 * src/structures/packimg.rs for the 32-byte header and the little endian size
 * word at 0x10.  No vendor documentation for this container was found, and
 * there is no checksum of any kind in the header - see the note in
 * xx_packimg.c about what that costs the reader.
 */

#ifndef XXFCLIB_FORMAT_PACKIMG_H
#define XXFCLIB_FORMAT_PACKIMG_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_PACKIMG_TAG "--PaCkImGs--"
#define XX_PACKIMG_TAG_SIZE 12U
#define XX_PACKIMG_HEADER_SIZE 32U
#define XX_PACKIMG_SIZE_OFFSET 0x10U

typedef struct xx_packimg xx_packimg;
typedef struct xx_packimg xx_packimg_t;
typedef struct xx_packimg XPackimg;

struct xx_packimg {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t data_size;   /**< The payload size word at 0x10. */
    uint32_t unknown;     /**< The word at 0x0C, reported unchanged. */
    int64_t data_offset;  /**< base_address + 32, or -1. */
    int64_t archive_end;  /**< data_offset + data_size, or -1. */
    void *internal;
};

XXFC_API void xx_packimg_init(xx_packimg *packimg, xx_io_device *dev,
                              int64_t base_address);
XXFC_API xx_packimg *xx_packimg_create(xx_io_device *dev,
                                       int64_t base_address);
XXFC_API void xx_packimg_destroy(xx_packimg *packimg);
XXFC_API void xx_packimg_free(xx_packimg *packimg);

XXFC_API bool xx_packimg_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_packimg_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_packimg_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_packimg_get_number_of_archive_records(Abstractformat *self,
                                                           xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_packimg_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_packimg_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_packimg_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_packimg_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_packimg_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_packimg_get_number_of_records(const xx_packimg *packimg);
XXFC_API uint64_t xx_packimg_get_number_of_members(const xx_packimg *packimg);
XXFC_API uint32_t xx_packimg_get_data_size(const xx_packimg *packimg);
XXFC_API int64_t xx_packimg_get_archive_end(const xx_packimg *packimg);

static inline Abstractformat *xx_packimg_to_format(xx_packimg *packimg) {
    return packimg ? &packimg->format : NULL;
}
static inline void XPackimg_init(xx_packimg *packimg, xx_io_device *dev,
                                 int64_t base_address) {
    xx_packimg_init(packimg, dev, base_address);
}
static inline xx_packimg *XPackimg_create(xx_io_device *dev,
                                          int64_t base_address) {
    return xx_packimg_create(dev, base_address);
}
static inline void XPackimg_free(xx_packimg *packimg) {
    xx_packimg_free(packimg);
}
static inline bool XPackimg_is_valid(xx_packimg *packimg, xx_pd_struct *pd) {
    return packimg ? xx_format_is_valid(&packimg->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PACKIMG_H */
