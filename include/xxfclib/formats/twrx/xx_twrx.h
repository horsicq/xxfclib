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
 *   0x08  u16 LE  method: 0 stored, 6 PKWARE DCL, 8 Deflate
 *   0x0a  u32 LE  tag, opaque (constant within an archive, often zero)
 *   0x0e  u32 LE  check: reflected CRC-32 (poly 0xEDB88320, seed 0, no
 *                 final complement) of the packed payload; 0 = not stored
 *   0x12  u32 LE  packed size, the bytes that follow the name
 *   0x16  u32 LE  unpacked size
 *   0x1a  u32 LE  name length
 *   0x1e  char[name length] member name, CP437, not NUL terminated
 *   then the payload, `packed size` bytes; the next block starts right after.
 *
 * Payloads:
 *   method 0  the bytes themselves; packed size equals unpacked size;
 *   method 6  one bare PKWARE DCL (explode) stream, "00 04..06" header,
 *             that ends exactly at the payload's end;
 *   method 8  u16 8, u32 (packed size - 6), then one raw Deflate stream
 *             that ends exactly at the payload's end.
 *
 * Extraction refuses a member whose stream leaves bytes over or needs bytes
 * past its payload, whose output differs from the unpacked size, whose
 * check fails, or whose name is absolute, drive-qualified, holds a ".."
 * component or a control character, or names a Windows device (CON, NUL,
 * AUX, PRN, COMn, LPTn, ...).  Such a member is still listed.  Stored and
 * Deflate members hold only their packed bytes in memory; a DCL member is
 * measured before its output buffer is allocated.
 *
 * Derived from the 22-archive, 225-member reference corpus: the chain tiles
 * every file to the last byte; 89 stored, 46 DCL and 90 Deflate members.
 * The check is present on all 135 stored and DCL members and matches every
 * one; the 90 Deflate members come from producers that leave it zero.
 * Member names are DOS 8.3; 0xF6 (CP437 U+00F7) is the producer's
 * part-number marker (e.g. "MARIO\xF6A.V2L") and is kept as U+00F7.
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
