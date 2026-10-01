/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_majiro.h @brief Native Majiro resource archive reader. */
#ifndef XXFCLIB_FORMAT_MAJIRO_H
#define XXFCLIB_FORMAT_MAJIRO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MajiroArcV1.000, V2.000 and V3.000 game-resource archives (.arc).
 * The little-endian header contains the file count at 16, the name-table
 * offset at 20, and the payload offset at 24. Index records start at 28:
 * v1 has {u32 name hash, u32 offset} and a final sentinel record; v2 has
 * {u32 name hash, u32 offset, u32 size}; v3 uses an eight-byte name hash.
 * Names are consecutive NUL-terminated CP932 byte strings. Payloads are
 * stored and extracted exactly as written, including empty members.
 *
 * All names and member ranges are validated before enumeration. CP932
 * bytes >=128 and '%' are escaped as "%XX" for portable, reversible names.
 * A CP932 trail backslash stays part of its escaped double-byte character;
 * other backslashes become '/'. Repeated names, compared without ASCII
 * case, receive "%_<index>" before the extension. Unsafe paths are listed
 * but refused for filesystem extraction. Original script/image payloads
 * are preserved; embedded object decoding is outside the container reader.
 *
 * Limits: 0xFFFFF members, 4096 bytes per raw name, 16 MiB raw name table,
 * 64 MiB expanded names. Calls preserve a known input-device cursor.
 * Files use exclusive short sibling stages and atomic publication with
 * explicit overwrite. Extraction options resolve operation values first,
 * then format-wide parameters.
 */
typedef struct xx_majiro {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t version;
    int64_t names_offset; /**< Relative to the archive start. */
    int64_t data_offset;  /**< Relative to the archive start. */
} xx_majiro;

typedef xx_majiro xx_majiro_t;
typedef xx_majiro XMajiro;

XXFC_API void xx_majiro_init(xx_majiro *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_majiro *xx_majiro_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_majiro_destroy(xx_majiro *archive);
XXFC_API void xx_majiro_free(xx_majiro *archive);
XXFC_API bool xx_majiro_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_majiro_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_majiro_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_majiro_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_majiro_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_majiro_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_majiro_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
/** Copy the current member to a caller-owned device; NULL verifies it only.
 * Destination must differ from the input device. Options limiting member
 * size and extraction buffers are also honored by this direct C API. */
XXFC_API bool xx_majiro_unpack_current_archive_record_to_device(
    Abstractformat *self, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd);
XXFC_API bool xx_majiro_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_majiro_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif
#endif
