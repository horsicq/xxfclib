/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_wrzl.h
 *  @brief "WRZL" single-stream compressed file.
 */

/* WHERE THE LAYOUT COMES FROM.  U3's recognition predicate (FUN_005710d0,
 * reached from VMT slot 0 at 0x005713c0):
 *
 *   u32 @ 0x00 == 0x4c5a5257  ("WRZL")
 *   i32 @ 0x04 >= 0
 *   u16 @ 0x08 != 0
 *   byte @ 0x0a: (byte - 0x40) must be below 0x48, and (byte - 0x20) must be
 *                set in a 128-bit table at 0x00571138
 *
 *   header, 12 bytes at offset 0, little endian:
 *     0x00  4    "WRZL"
 *     0x04  u32  UNCOMPRESSED size
 *     0x08  u16  unidentified, nonzero
 *     0x0a  1    0x40 in every sample
 *     0x0b  1    0x00 or 0x02
 *     0x0c  ..   packed bytes to end of file
 *
 * FIELD 0x04 IS CONFIRMED.  The reference unpacker reports 14118 bytes for
 * DUMMY.DA$ and that is exactly the u32 at offset 4 of that file; the same
 * holds for the other five samples in F:\ARC\ARC\WRZL (67498, 139350, 68054,
 * 3646078, 132022).  So the uncompressed size published by this reader is a
 * real stored field, not an estimate.
 *
 * THE BIT TABLE AT 0x0a IS NOT PORTED, and this is deliberate.  The table
 * lives in U3's data segment; the shipped binary is packed, so its contents
 * could not be read out, and inventing a membership set would be guessing.
 * What IS ported is U3's own range test - the byte must lie in 0x40..0x87 -
 * which is the part that can be justified.  All six corpus samples carry
 * 0x40.  The consequence is stated plainly: this reader is very slightly more
 * permissive at that one byte than U3 is.  With a four-byte magic in front of
 * it that is not a practical risk.
 *
 * WHAT THIS READER DOES NOT DO.  The codec is not identified.  It is not one
 * of the stream decoders this library carries, and the container offers no
 * CRC and no other anchor by which a candidate decode could be checked -
 * only the plaintext length.  The reference unpacker does decode it, so the
 * algorithm exists and is recoverable; it is simply not recovered here, and
 * unpacking fails closed rather than emitting a guess.
 *
 * The record IS published even so: the stream's exact packed extent and its
 * real uncompressed length are facts the container states, and a caller
 * listing this file should see them.  The member has no name in the container
 * - the reference unpacker falls back to the host file's name - so a neutral
 * one is published rather than one invented from outside the format.
 */

#ifndef XXFCLIB_FORMAT_WRZL_H
#define XXFCLIB_FORMAT_WRZL_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Header magic and size. */
#define XX_WRZL_SIGNATURE "WRZL"
#define XX_WRZL_SIGNATURE_SIZE 4U
#define XX_WRZL_HEADER_SIZE 12U
/** Range U3 imposes on the byte at 0x0a. */
#define XX_WRZL_MODE_MIN 0x40U
#define XX_WRZL_MODE_MAX 0x87U
/** Ceiling on the declared uncompressed size. */
#define XX_WRZL_MAX_UNCOMPRESSED_SIZE ((int64_t)1024 * 1024 * 1024)

typedef struct xx_wrzl xx_wrzl;
typedef struct xx_wrzl xx_wrzl_t;
typedef struct xx_wrzl XWrzl;

struct xx_wrzl {
    Abstractformat format; /**< Base format structure (first member). */
    int64_t packed_offset; /**< Absolute offset of the packed stream. */
    int64_t packed_size;   /**< Packed length, measured from the file. */
    int64_t unpacked_size; /**< Stored uncompressed length (u32 at 0x04). */
    uint16_t opaque_08;    /**< u16 at 0x08.  Never interpreted. */
    uint8_t mode;          /**< Byte at 0x0a. */
    uint8_t flags;         /**< Byte at 0x0b. */
};

XXFC_API void xx_wrzl_init(xx_wrzl *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_wrzl *xx_wrzl_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_wrzl_destroy(xx_wrzl *archive);
XXFC_API void xx_wrzl_free(xx_wrzl *archive);

XXFC_API bool xx_wrzl_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_wrzl_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_wrzl_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_wrzl_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_wrzl_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_wrzl_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_wrzl_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_wrzl_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_wrzl_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Absolute offset of the packed stream, or -1 before handle_base_info. */
XXFC_API int64_t xx_wrzl_get_packed_offset(const xx_wrzl *archive);
/** Packed length, or -1 before handle_base_info. */
XXFC_API int64_t xx_wrzl_get_packed_size(const xx_wrzl *archive);
/** Stored uncompressed length, or -1 before handle_base_info. */
XXFC_API int64_t xx_wrzl_get_unpacked_size(const xx_wrzl *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_WRZL_H */
