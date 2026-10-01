/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ypf.h @brief YU-RIS engine YPF resource archive reader. */

#ifndef XXFCLIB_FORMAT_YPF_H
#define XXFCLIB_FORMAT_YPF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * YPF archive (YU-RIS visual-novel engine, ".ypf").  All fields little-endian;
 * every offset is relative to the start of the "YPF\0" header.
 *
 * Header (0x20 bytes):
 *   0x00  4  "YPF\0"
 *   0x04  4  engine version (seen 0xDE .. 0x1F4)
 *   0x08  4  entry count
 *   0x0C  4  index size field (writers store 0x20 + index length)
 *   0x10 16  zero
 *
 * Index at 0x20, `count` variable-length entries:
 *   4  name hash
 *   1  name length, obfuscated: NOT, then a version-dependent byte-pair swap
 *   n  name bytes, each XOR a one-byte key (cp932, '\' separators)
 *   1  file type (ybn/bmp/png/...; informational)
 *   1  packed flag: 0 stored, non-zero zlib (RFC 1950)
 *   4  unpacked size
 *   4  stored (packed) size
 *   4  data offset
 *   4  checksum of the stored bytes
 *   e  extra: 8 bytes for version 0xDE, 4 for version >= 0x1D9 (the high
 *      half of a 64-bit offset), otherwise none
 *
 * The swap table and the XOR key are not stored.  The table is chosen from
 * the version (GARbro's guess) with the other known tables as fallbacks, and
 * the key is taken from the first name, assuming it ends in ".xxx".  A
 * candidate is kept only when the whole index decodes to control-free names
 * whose data lie inside the file.
 *
 * Embedded YPF data (a game EXE carrying it after a "YSER" block) is found by
 * the format search through the "YPF\0" anchor; the reader itself opens a YPF
 * at base_address only.  YSTB script decryption needs a per-game key and is
 * not applied: .ybn members are extracted as stored.
 */
typedef struct xx_ypf {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t version;
} xx_ypf;

typedef xx_ypf xx_ypf_t;

XXFC_API void xx_ypf_init(xx_ypf *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_ypf *xx_ypf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ypf_destroy(xx_ypf *archive);
XXFC_API void xx_ypf_free(xx_ypf *archive);

XXFC_API bool xx_ypf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ypf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ypf_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_ypf_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ypf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ypf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ypf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ypf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ypf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_YPF_H */
