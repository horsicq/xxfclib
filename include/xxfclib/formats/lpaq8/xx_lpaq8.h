/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_lpaq8.h @brief LPAQ8 compressed stream reader (RAM-only decoding). */

#ifndef XXFCLIB_FORMAT_LPAQ8_H
#define XXFCLIB_FORMAT_LPAQ8_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An LPAQ8 stream.
 *
 * LPAQ8 is Matt Mahoney's lightweight PAQ variant. Unlike BCM the container
 * carries a real nine-byte header -- magic, a memory-level digit, the original
 * size and a data mode -- so those are reported here. What follows is a single
 * context-mixing bitstream, which this reader decodes with a separately licensed
 * RAM-only helper. The decoded stream is exposed as one archive member.
 *
 * A NULL destination still decodes the complete stream under the operation's
 * memory limit. This wire format does not contain a payload checksum.
 */
typedef struct xx_lpaq8 {
    Abstractformat format;
    const xx_list_s *parse_options;
    uint64_t number_of_records;
    bool incomplete;
    const char *note;
    uint8_t level;             /**< Memory level, the ASCII digit decoded to 0..9. */
    uint8_t data_mode;         /**< Stored mode byte, 0..2. */
    uint32_t uncompressed_size; /**< Original size in bytes, big-endian in the file. */
} xx_lpaq8;

typedef xx_lpaq8 xx_lpaq8_t;
typedef xx_lpaq8 XLpaq8;

XXFC_API void xx_lpaq8_init(xx_lpaq8 *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_lpaq8 *xx_lpaq8_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_lpaq8_destroy(xx_lpaq8 *archive);
XXFC_API void xx_lpaq8_free(xx_lpaq8 *archive);

XXFC_API bool xx_lpaq8_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lpaq8_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_lpaq8_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_lpaq8_get_number_of_archive_records(Abstractformat *self,
                                                          xx_pd_struct *pd);

/** @brief Memory level 0..9, or 0 before the header is read. */
XXFC_API uint8_t xx_lpaq8_get_level(const xx_lpaq8 *archive);
/** @brief Stored data mode, 0..2. */
XXFC_API uint8_t xx_lpaq8_get_data_mode(const xx_lpaq8 *archive);
/** @brief Original size the header records, in bytes. */
XXFC_API uint32_t xx_lpaq8_get_uncompressed_size(const xx_lpaq8 *archive);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lpaq8_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_lpaq8_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lpaq8_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lpaq8_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_lpaq8_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_LPAQ8_H */
