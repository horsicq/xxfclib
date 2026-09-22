/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_twrx.h @brief TWRX installer container reader. */

#ifndef XXFCLIB_FORMAT_TWRX_H
#define XXFCLIB_FORMAT_TWRX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The TWRX installer container (*.TZF).
 *
 * There is no file header and no directory: the archive is a chain of
 * self-describing blocks, one per member, that tiles the file exactly.  Each
 * block opens on its own magic:
 *
 *   0x00  char[4] "TWRX"
 *   0x04  u16 LE  version, 0x0100 in every known block
 *   0x06  u16 LE  zero in every known block
 *   0x08  u16 LE  method: 0 stored, 6 and 8 are the producer's own codecs
 *   0x0a  8 bytes stamp; zero on some producers, opaque on others
 *   0x12  u32 LE  packed size, the bytes that follow the name
 *   0x16  u32 LE  unpacked size
 *   0x1a  u32 LE  name length
 *   0x1e  char[name length] member name, ANSI, not NUL terminated
 *   then the payload, `packed size` bytes; the next block starts right after.
 *
 * Derived from the 22-archive, 225-member reference corpus: the chain tiles
 * every one of the 22 files to the last byte, and the method word takes only
 * three values there.
 *
 * Method 0 is stored and is decoded here - packed size equals unpacked size
 * in all 89 of its corpus members, which is the anchor.  Methods 6 and 8 are
 * compressed with codecs that are not identified (method 8 payloads open on
 * a six-byte `08 00` plus u32 sub-header, method 6 payloads on `00 05`);
 * those members are listed with their real names and sizes and unpack fails
 * closed for them.
 */
typedef struct xx_twrx {
    Abstractformat format;
    uint64_t number_of_records;
} xx_twrx;

typedef xx_twrx xx_twrx_t;

XXFC_API void xx_twrx_init(xx_twrx *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_twrx *xx_twrx_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_twrx_destroy(xx_twrx *archive);
XXFC_API void xx_twrx_free(xx_twrx *archive);

XXFC_API bool xx_twrx_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_twrx_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_twrx_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_twrx_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_twrx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_twrx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_twrx_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_twrx_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_twrx_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TWRX_H */
