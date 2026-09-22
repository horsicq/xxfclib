/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_wince.h @brief Windows CE ROM image (.bin) reader. */

/* The Windows CE ".bin" ROM image is the transport format produced by the
 * Platform Builder toolchain and consumed by viewbin/dumprom. It is a flat
 * sequence of "copy this many bytes to this physical address" records, so it
 * behaves as an archive of address-named blobs rather than as a filesystem.
 *
 *   image header (15 bytes, little endian)
 *     +0   "B000FF\n"  the seven byte ASCII signature
 *     +7   u32  image start, the physical address the image loads at
 *     +11  u32  image length, the number of bytes the image spans
 *
 *   record header (12 bytes, little endian), repeated
 *     +0   u32  address, the physical address this record's bytes load at
 *     +4   u32  length, the number of payload bytes that follow the header
 *     +8   u32  checksum, the plain 32-bit wrapping sum of those bytes
 *     +12  `length` payload bytes
 *
 * A record whose address field is zero terminates the chain; in a real image
 * its length field carries the entry point and no payload follows it.
 *
 * Every record field is attacker controlled. The walker therefore bounds the
 * payload against the device size before seeking, caps the record count, and
 * stops at the terminator - a zero length record still advances the cursor by
 * the twelve header bytes, so the chain cannot spin in place, but the cap is
 * kept as a second line of defence. The per record checksum is verified: it
 * is a single pass over bytes that are read anyway, and it is what separates
 * a real image from seven matching ASCII bytes in unrelated data.
 */

#ifndef XXFCLIB_FORMAT_WINCE_H
#define XXFCLIB_FORMAT_WINCE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_wince xx_wince;
typedef struct xx_wince xx_wince_t;
typedef struct xx_wince XWince;

struct xx_wince {
    Abstractformat format;
    uint64_t number_of_records;  /**< Payload records, terminator excluded. */
    uint64_t number_of_members;
    uint32_t image_start;   /**< Physical load address from the file header. */
    uint32_t image_length;  /**< Declared image span, unverified. */
    int64_t archive_end;    /**< One byte past the terminator record, or -1. */
    void *internal;
};

XXFC_API void xx_wince_init(xx_wince *wince, xx_io_device *dev,
                            int64_t base_address);
XXFC_API xx_wince *xx_wince_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_wince_destroy(xx_wince *wince);
XXFC_API void xx_wince_free(xx_wince *wince);

XXFC_API bool xx_wince_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_wince_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_wince_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_wince_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_wince_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wince_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wince_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wince_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wince_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_wince_get_number_of_records(const xx_wince *wince);
XXFC_API uint64_t xx_wince_get_number_of_members(const xx_wince *wince);
XXFC_API uint32_t xx_wince_get_image_start(const xx_wince *wince);
XXFC_API uint32_t xx_wince_get_image_length(const xx_wince *wince);
XXFC_API int64_t xx_wince_get_archive_end(const xx_wince *wince);

static inline Abstractformat *xx_wince_to_format(xx_wince *wince) {
    return wince ? &wince->format : NULL;
}
static inline void XWince_init(xx_wince *wince, xx_io_device *dev,
                               int64_t base_address) {
    xx_wince_init(wince, dev, base_address);
}
static inline xx_wince *XWince_create(xx_io_device *dev,
                                      int64_t base_address) {
    return xx_wince_create(dev, base_address);
}
static inline void XWince_free(xx_wince *wince) { xx_wince_free(wince); }
static inline bool XWince_is_valid(xx_wince *wince, xx_pd_struct *pd) {
    return wince ? xx_format_is_valid(&wince->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WINCE_H */
