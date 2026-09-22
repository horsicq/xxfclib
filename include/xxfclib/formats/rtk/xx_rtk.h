/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_rtk.h @brief RTK (Realtek) firmware header reader. */

/* The RTK header prefixes Realtek SoC firmware images and is recognised by
 * binwalk's "RTK firmware header" signature. It is a 32-byte little-endian
 * prologue starting with the ASCII magic "RTK0".
 *
 *   +0   u32  magic, the ASCII bytes "RTK0"
 *   +4   u32  image size
 *   +8   u32  checksum, algorithm not published
 *   +12  u32  unknown
 *   +16  u32  header size, NOT counting the four magic bytes
 *   +20  u32  unknown
 *   +24  u32  unknown
 *   +28  u32  identifier
 *
 * The header-size field excludes the magic, so the payload begins at
 * base_address + header_size + 4. That +4 adjustment is the behaviour of the
 * published parser and is reproduced here; the three "unknown" words and the
 * checksum algorithm are genuinely undocumented, so they are surfaced as raw
 * values and the checksum is NOT verified.
 *
 * The image-size field is taken as the length of the payload that follows the
 * header. Every field is attacker controlled, so the reader requires the
 * effective header size to cover the fixed 32 bytes and requires the payload
 * to lie wholly inside the device before it exposes a record.
 */

#ifndef XXFCLIB_FORMAT_RTK_H
#define XXFCLIB_FORMAT_RTK_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_rtk xx_rtk;
typedef struct xx_rtk xx_rtk_t;
typedef struct xx_rtk XRtk;

struct xx_rtk {
    Abstractformat format;
    uint64_t number_of_records;  /**< Always 1 when the payload is non-empty. */
    uint64_t number_of_members;
    uint32_t image_size;    /**< Declared payload length. */
    uint32_t checksum;      /**< Raw field; the algorithm is unpublished. */
    uint32_t header_size;   /**< Raw field, excluding the four magic bytes. */
    uint32_t identifier;    /**< Raw field at +28. */
    int64_t payload_offset; /**< base_address + header_size + 4, or -1. */
    int64_t archive_end;    /**< payload_offset + image_size, or -1. */
    void *internal;
};

XXFC_API void xx_rtk_init(xx_rtk *rtk, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_rtk *xx_rtk_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_rtk_destroy(xx_rtk *rtk);
XXFC_API void xx_rtk_free(xx_rtk *rtk);

XXFC_API bool xx_rtk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rtk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_rtk_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_rtk_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rtk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rtk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rtk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rtk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rtk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_rtk_get_number_of_records(const xx_rtk *rtk);
XXFC_API uint32_t xx_rtk_get_image_size(const xx_rtk *rtk);
XXFC_API uint32_t xx_rtk_get_checksum(const xx_rtk *rtk);
XXFC_API uint32_t xx_rtk_get_header_size(const xx_rtk *rtk);
XXFC_API uint32_t xx_rtk_get_identifier(const xx_rtk *rtk);
XXFC_API int64_t xx_rtk_get_payload_offset(const xx_rtk *rtk);
XXFC_API int64_t xx_rtk_get_archive_end(const xx_rtk *rtk);

static inline Abstractformat *xx_rtk_to_format(xx_rtk *rtk) {
    return rtk ? &rtk->format : NULL;
}
static inline void XRtk_init(xx_rtk *rtk, xx_io_device *dev,
                             int64_t base_address) {
    xx_rtk_init(rtk, dev, base_address);
}
static inline xx_rtk *XRtk_create(xx_io_device *dev, int64_t base_address) {
    return xx_rtk_create(dev, base_address);
}
static inline void XRtk_free(xx_rtk *rtk) { xx_rtk_free(rtk); }
static inline bool XRtk_is_valid(xx_rtk *rtk, xx_pd_struct *pd) {
    return rtk ? xx_format_is_valid(&rtk->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RTK_H */
