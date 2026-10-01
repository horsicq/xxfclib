/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_noa.h @brief Entis GLS / ERISA NOA resource archive reader. */

#ifndef XXFCLIB_FORMAT_NOA_H
#define XXFCLIB_FORMAT_NOA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Entis GLS / ERISA-Archive (*.noa, *.dat, *.rsa, *.arc, *.emc).
 *
 * Every field is LITTLE endian:
 *   0x00  "Entis\x1a\0\0" (or "VIST\x1a\0\0\0")
 *   0x08  u32 file id, always 0x02000400
 *   0x0C  u32 reserved
 *   0x10  char[48] description: "ERISA-Archive file" or, in the older
 *         EMSAC generation, "EMSAC-Binary Archive"
 *   0x40  the root directory: a record
 *
 * A record is char[8] tag, u64 body length, body.  A directory record has
 * the tag "DirEntry" and the body
 *   u32 count, then per entry:
 *     u64 original size   u32 attribute   u32 encode type
 *     u64 record offset, relative to the start of this DirEntry record
 *     u64 time stamp      u32 extra length, extra bytes
 *     u32 name length, name bytes (Shift-JIS, NUL terminated)
 * Attribute 0x10 is a sub-directory whose record body is a nested DirEntry
 * record; 0x20 and 0x40 end the directory; anything else is a file.  A
 * file's record body is its data: stored as-is (encode type 0) or
 * ERISA-Nemesis coded (0x80000010: the coded stream, then 4 more bytes).
 * Other encode types are password ciphers (BSHF and friends): those members
 * are listed, flagged encrypted, and refused on extraction.
 *
 * Detection needs the magic, the file id and "DirEntry" at 0x40; the probe
 * then walks the whole tree (bounded: 32 levels, 65536 directories,
 * 262144 members, each directory body at most 64 MiB, 64 MiB live) and
 * refuses a directory that is visited twice.
 *
 * Member names follow this library's Shift-JIS convention (sar_ns, nsa,
 * ns2): '\\' becomes '/', a double-byte character becomes "%XX%XX", any
 * other byte >= 0x80 and '%' become "%XX"; a repeated path (compared
 * case-insensitively) gets "%_<index>" before its extension; unsafe paths
 * are listed but refused on extraction.
 */
typedef struct xx_noa {
    Abstractformat format;
    uint64_t number_of_records;
} xx_noa;

typedef xx_noa xx_noa_t;

XXFC_API void xx_noa_init(xx_noa *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_noa *xx_noa_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_noa_destroy(xx_noa *archive);
XXFC_API void xx_noa_free(xx_noa *archive);

XXFC_API bool xx_noa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_noa_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_noa_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_noa_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_noa_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_noa_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_noa_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_noa_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_noa_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NOA_H */
