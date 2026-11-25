/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_malie_lib.h @brief Native recursive unencrypted Malie LIB/LIBU reader. */
#ifndef XXFCLIB_FORMAT_MALIE_LIB_H
#define XXFCLIB_FORMAT_MALIE_LIB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/**
 * Unencrypted Malie LIB resource archives: "LIB\0", 16-byte header and a
 * positive signed 16-bit count at 8. Each 48-byte directory entry contains
 * a fixed 36-byte CP932 name, u32 size at 36 and u32 offset at 40. Offsets
 * are relative to that immediate container. Every name and range in a
 * directory is checked before following any nested extensionless LIB member.
 * Enumeration lists leaf files with complete paths; other member bytes are
 * preserved exactly, including empty files and extension-bearing containers.
 * Extensionless resources with a LIB signature recurse only when their full
 * immediate child index is valid; invalid child indexes remain opaque leaves.
 * Unencrypted LIBU uses a 16-byte "LIBU" header with a 32-bit count at 8;
 * each 80-byte entry has a 34-code-unit UTF-16LE name, u32 size and i64
 * offset relative to its immediate container. Valid nested LIBU directories
 * are listed recursively. UTF-16 names become UTF-8; malformed surrogates
 * are rejected and literal '%' is escaped as "%25". Encrypted LIBP/LIBU
 * variants require title-specific keys and are not decoded.
 *
 * CP932 bytes and '%' use reversible "%XX" escapes; a double-byte trail
 * backslash stays escaped while path backslashes become '/'. ASCII-case
 * duplicate leaf paths and leaves conflicting with implicit directories
 * receive "%_<index>" before their extension. Unsafe
 * paths remain listable but cannot be extracted to filesystem destinations.
 *
 * Limits: depth 64 (root included), 32767 records per directory, 0xFFFFF
 * total leaves/directories, 4096 escaped path bytes, 64 MiB combined leaf
 * names and 16 MiB concurrently held indices. All known input cursors are
 * preserved. Filesystem extraction stages output before atomic publication;
 * existing files require the overwrite option. Inner resource transforms
 * and title-specific encrypted LIBP/LIBU decryptors are outside this reader.
 */
typedef struct xx_malie_lib {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_directories;
} xx_malie_lib;
typedef xx_malie_lib xx_malie_lib_t;
XXFC_API void xx_malie_lib_init(xx_malie_lib *, xx_io_device *, int64_t);
XXFC_API xx_malie_lib *xx_malie_lib_create(xx_io_device *, int64_t);
XXFC_API void xx_malie_lib_destroy(xx_malie_lib *);
XXFC_API void xx_malie_lib_free(xx_malie_lib *);
XXFC_API bool xx_malie_lib_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_malie_lib_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_malie_lib_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_malie_lib_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_malie_lib_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_malie_lib_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_malie_lib_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
/** Copy a leaf to a caller-owned device; NULL verifies it only. */
XXFC_API bool xx_malie_lib_unpack_current_archive_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
XXFC_API bool xx_malie_lib_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_malie_lib_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
#ifdef __cplusplus
}
#endif
#endif
