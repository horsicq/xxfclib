/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_godot_engine_pck.h @brief Godot Engine resource pack (PCK) reader. */

#ifndef XXFCLIB_FORMAT_GODOT_ENGINE_PCK_H
#define XXFCLIB_FORMAT_GODOT_ENGINE_PCK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A Godot Engine resource pack: a header, a directory of "res://" paths and
 * the stored member files.  Everything is little-endian.
 *
 *   +0x00  char[4]  "GDPC"
 *   +0x04  u32      pack format version: 0 (Godot 1.x/2.x), 1 (3.x),
 *                   2 (4.0..4.4), 3 (4.5), 4 (later 4.x)
 *   +0x08  u32      engine major, +0x0C minor, +0x10 patch
 *
 *   versions 0 and 1
 *   +0x14  u32[16]  reserved, zero
 *   +0x54  u32      file count, and the directory right behind it
 *
 *   version 2
 *   +0x14  u32      pack flags: 1 directory encrypted, 2 file base counted
 *                   from the pack start (otherwise from the start of the file
 *                   that holds the pack)
 *   +0x18  u64      file base
 *   +0x20  u32[16]  reserved, zero
 *   +0x60  u32      file count, and the directory right behind it
 *
 *   versions 3 and 4
 *   +0x14  u32      pack flags (as version 2, plus 4 sparse bundle; the file
 *                   base is always counted from the pack start)
 *   +0x18  u64      file base, from the pack start
 *   +0x20  u64      directory offset, from the pack start (the directory
 *                   follows the member data)
 *   +0x28  u32[16]  reserved, zero; version 4 keeps a 32-byte salt in the
 *                   first half when the pack is an encrypted sparse bundle
 *   at the directory offset: u32 file count, then the directory
 *
 *   directory entry
 *     u32      path length L; Godot pads the path with NULs to a multiple of
 *              four bytes and counts the padding
 *     u8[L]    UTF-8 path, normally "res://...", ending at the first NUL
 *     u64      offset: versions 0/1 from the start of the file that holds
 *              the pack, versions 2..4 from the file base
 *     u64      size
 *     u8[16]   MD5 of the stored member (all zero when a writer left it out)
 *     u32      versions 2..4 only: flags, 1 encrypted, 2 removal (a patch
 *              pack entry that deletes a path and carries no data), 4 delta
 *              (versions 3/4: the data is a delta against the member of an
 *              earlier pack; size and MD5 describe the delta)
 *
 * A sparse bundle keeps only the directory: the exporter writes each member
 * as a file of its own next to the pack and stores offset zero.  Its members
 * are listed but cannot be extracted from the pack, and neither can a delta
 * (it is not the member) or an encrypted member (below).
 *
 * An encrypted member, and an encrypted directory (which starts after the
 * clear file count), is stored as u8[16] MD5, u64 plain size, u8[16] IV and
 * the AES-256-CFB ciphertext padded to a multiple of 16 bytes.  Without the
 * project key neither can be read: an encrypted member is listed but not
 * extracted, and a pack with an encrypted directory is recognised with no
 * records.
 *
 * Godot's "embed PCK" export appends the pack to the engine executable and
 * ends the file with u64 pack size + "GDPC"; Windows builds also map the pack
 * into a PE section named "pck", which stays valid after a signature has
 * been appended behind it.  The reader finds the pack either at its own base
 * address, through that trailer, or through that PE section, in that order,
 * and reads nothing else of the executable.
 */

/** Where the pack header was found. */
typedef enum xx_godot_engine_pck_placement_e {
    XX_GODOT_ENGINE_PCK_STANDALONE = 0, /**< "GDPC" at the base address. */
    XX_GODOT_ENGINE_PCK_TRAILER = 1,    /**< Through the end-of-file trailer. */
    XX_GODOT_ENGINE_PCK_PE_SECTION = 2  /**< Through the PE section "pck". */
} xx_godot_engine_pck_placement_t;

#define XX_GODOT_ENGINE_PCK_FLAG_DIR_ENCRYPTED 0x00000001U
#define XX_GODOT_ENGINE_PCK_FLAG_REL_FILEBASE 0x00000002U
#define XX_GODOT_ENGINE_PCK_FLAG_SPARSE_BUNDLE 0x00000004U
#define XX_GODOT_ENGINE_PCK_FILE_ENCRYPTED 0x00000001U
#define XX_GODOT_ENGINE_PCK_FILE_REMOVAL 0x00000002U
#define XX_GODOT_ENGINE_PCK_FILE_DELTA 0x00000004U

/** Longest stored path accepted, padding included. */
#define XX_GODOT_ENGINE_PCK_MAX_PATH 4096U
/** Most directory entries accepted. */
#define XX_GODOT_ENGINE_PCK_MAX_ENTRIES 1000000U

typedef struct xx_godot_engine_pck {
    Abstractformat format;
    uint64_t number_of_records;  /**< Listed members (removal entries are not). */
    uint32_t pack_version;
    uint32_t engine_major;
    uint32_t engine_minor;
    uint32_t engine_patch;
    uint32_t pack_flags;         /**< Zero for versions 0 and 1. */
    uint32_t file_count;         /**< Directory entries as declared. */
    int64_t pack_offset;         /**< Absolute device offset of "GDPC". */
    int64_t file_base;           /**< Absolute origin member offsets add to. */
    int64_t directory_offset;    /**< Absolute offset of the file count. */
    xx_godot_engine_pck_placement_t placement;
    bool directory_encrypted;
} xx_godot_engine_pck;

typedef xx_godot_engine_pck xx_godot_engine_pck_t;

XXFC_API void xx_godot_engine_pck_init(xx_godot_engine_pck *archive,
                                       xx_io_device *device,
                                       int64_t base_address);
XXFC_API xx_godot_engine_pck *xx_godot_engine_pck_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_godot_engine_pck_destroy(xx_godot_engine_pck *archive);
XXFC_API void xx_godot_engine_pck_free(xx_godot_engine_pck *archive);

XXFC_API bool xx_godot_engine_pck_check_is_valid(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API bool xx_godot_engine_pck_handle_base_info(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API int64_t xx_godot_engine_pck_get_format_size(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API uint64_t xx_godot_engine_pck_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_godot_engine_pck_create_archive_records_reading(Abstractformat *self,
                                                   const xx_list_s *options,
                                                   xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_godot_engine_pck_get_current_archive_record(Abstractformat *self,
                                               xx_archive_record_state *state);
XXFC_API bool xx_godot_engine_pck_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_godot_engine_pck_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_godot_engine_pck_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GODOT_ENGINE_PCK_H */
