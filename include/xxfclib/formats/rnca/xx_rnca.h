/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_rnca.h @brief Rob Northen "RNCA" multi-member archive reader. */

#ifndef XXFCLIB_FORMAT_RNCA_H
#define XXFCLIB_FORMAT_RNCA_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An RNCA archive: a directory of names followed by one complete RNC
 * ProPack stream per member.
 *
 * This is NOT the single-stream RNC ProPack format (magic "RNC\\1"/"RNC\\2");
 * it is the container Rob Northen's installers shipped on floppy sets, and it
 * carries its own eleven byte header:
 *
 *   +0   char[4]   "RNCA"
 *   +4   uint16be  offset of the first member, i.e. the size of the header
 *                  plus the whole directory
 *   +6   uint16be  directory check word (not validated here)
 *   +8   uint16be  a second copy of the +4 value
 *   +10  uint8     always zero
 *
 * The directory then runs from +11: each entry is a NUL-terminated name
 * followed by a uint32be absolute offset of that member's stream.  A zero byte
 * where a name would start ends the directory, and that byte is the last byte
 * before the first member.  Member offsets increase, and each member is a
 * self-contained RNC stream: method 0 is "RNC\\0" with an 8 byte header and a
 * uint32be stored length, methods 1 and 2 are the ordinary 18 byte ProPack
 * header and are decoded by the RNC reader.
 */
typedef struct xx_rnca {
    Abstractformat format;      /**< Base format structure (must be first). */
    uint64_t number_of_records; /**< Members listed in the directory. */
    int64_t directory_size;     /**< Bytes before the first member. */
    int64_t archive_end;        /**< Absolute end offset of the archive. */
} xx_rnca;

typedef xx_rnca xx_rnca_t;
typedef xx_rnca XRnca;

XXFC_API void xx_rnca_init(xx_rnca *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_rnca *xx_rnca_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_rnca_destroy(xx_rnca *archive);
XXFC_API void xx_rnca_free(xx_rnca *archive);

XXFC_API bool xx_rnca_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_rnca_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_rnca_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_rnca_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_rnca_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rnca_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rnca_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rnca_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rnca_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_rnca_get_number_of_records(const xx_rnca *archive);
XXFC_API int64_t xx_rnca_get_directory_size(const xx_rnca *archive);

static inline Abstractformat *xx_rnca_to_format(xx_rnca *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RNCA_H */
