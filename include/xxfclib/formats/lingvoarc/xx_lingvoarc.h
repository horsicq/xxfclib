/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lingvoarc.h @brief LINGVOARC installer container reader. */

#ifndef XXFCLIB_FORMAT_LINGVOARC_H
#define XXFCLIB_FORMAT_LINGVOARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The LINGVOARC installer container (ABBYY Lingvo distribution disks).
 *
 *   file header, 18 bytes at 0:
 *     0x00  char[9] "lingvoArc"
 *     0x09  char    version, '1' or '2'
 *     0x0a  6 bytes 00 fd 00 df 00 ff, constant
 *     0x10  u16 LE  member count, never zero
 *
 *   index at 0x12, one fixed-stride entry per member.  Version 1 uses a
 *   54-byte entry, version 2 a 109-byte one:
 *     0x00           char[46] / char[101]  member path, NUL terminated;
 *                    bytes after the terminator are stale producer-buffer
 *                    content and are ignored
 *     0x2e / 0x65    u32 LE absolute offset of the member's descriptor
 *     0x32 / 0x69    u32 LE timestamp
 *
 *   descriptor, at that offset.  Version 1 is 23 bytes, version 2 is 71:
 *     0x00           char[13] / char[61]  short name
 *     0x0d / 0x3d    u32 LE payload size
 *     0x11 / 0x41    u32 LE timestamp
 *     0x15 / 0x45    u16 LE method
 *   the payload follows the descriptor inline, `payload size` bytes.
 *
 * The layout is U3's own: its recognition predicate is the magic, the
 * constant six bytes and the non-zero count, and its walk is the index read
 * above (U3 VMT slot 0 at 0x0051d100 / FUN_0051cd70, slot 1 at 0x0051d120 /
 * FUN_0051cdc0).
 *
 * Payloads are nested FINEAR streams - a 17-byte header and an LHA -lh1- body
 * with a stored plaintext length and a CRC-16/ARC - and those are decoded and
 * verified here.  The descriptor's method word says whether a payload is
 * whole: 0 is a self-contained member, 6 is the first fragment of one that
 * continues on the next volume (FINEAR header present, body cut off at EOF),
 * and 2 is a continuation fragment whose header was left on the previous
 * volume.  A fragment cannot be decoded from one volume alone, so unpack
 * fails closed for methods 2 and 6.
 */
typedef struct xx_lingvoarc {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t container_version; /**< 1 or 2. */
} xx_lingvoarc;

typedef xx_lingvoarc xx_lingvoarc_t;

XXFC_API void xx_lingvoarc_init(xx_lingvoarc *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_lingvoarc *xx_lingvoarc_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_lingvoarc_destroy(xx_lingvoarc *archive);
XXFC_API void xx_lingvoarc_free(xx_lingvoarc *archive);

XXFC_API bool xx_lingvoarc_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_lingvoarc_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_lingvoarc_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_lingvoarc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lingvoarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lingvoarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lingvoarc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lingvoarc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lingvoarc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LINGVOARC_H */
