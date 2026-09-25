/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_chm.h @brief Microsoft Compiled HTML Help (CHM, ITSF) reader. */

#ifndef XXFCLIB_FORMAT_CHM_H
#define XXFCLIB_FORMAT_CHM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Microsoft "ITSF" storage file: .chm, and the .chi / .chw / .chq
 * files HTML Help keeps next to it.  All fields are little endian.
 *
 * Header, at the start of the file:
 *
 *   0x00  char[4] "ITSF"
 *   0x04  u32     version: 3 (header 0x60 bytes) or 2 (header 0x58 bytes)
 *   0x08  u32     header size
 *   0x0C  u32     0 or 1
 *   0x10  u32     time stamp, u32 language id, two GUIDs
 *   0x38  u64/u64 offset and size of header section 0 (0x1FE, 0, u64 file
 *                 size, 0, 0)
 *   0x48  u64/u64 offset and size of the directory
 *   0x58  u64     version 3: offset of content section 0; version 2 has no
 *                 field, section 0 then starts right behind the directory
 *
 * Directory: an 0x54-byte "ITSP" header (version 1, chunk size at +0x10,
 * chunk count at +0x2C), then that many chunks of that size.  "PMGL"
 * (listing) chunks hold the entries from offset 20 up to (chunk size - free
 * space); the chunk's last u16 counts them (0 in some compilers' output).
 * "PMGI" (index) chunks only speed up lookups and are skipped.  An entry is
 *
 *   encint name length, UTF-8 name, encint section, encint offset,
 *   encint size
 *
 * where an encint is big-endian base-128, high bit set on all bytes but the
 * last.  Names starting with '/' are the help file's own files ('/'-ended
 * ones are folders); names starting with "::" are storage metadata.
 *
 * Content section 0 is stored.  Every further section is named in
 * "::DataSpace/NameList" and lives in "::DataSpace/Storage/<name>/Content";
 * its "ControlData" ("LZXC", version 2 or 3, reset interval and window size
 * in 32 KiB units) and its reset table
 * ("Transform/{7FC28940-9D31-11D0-9B27-00A0C91E9C7C}/InstanceData/ResetTable":
 * version, entry count, entry size 8, table offset 0x28, u64 uncompressed
 * size, u64 compressed size, u64 frame size 0x8000, then the compressed
 * offset of every 32 KiB frame) describe one LZX stream.  The stream starts
 * over (fresh window, repeat offsets and E8 header) every "reset interval"
 * frames, and every frame decodes to 32 KiB (HTML Help Workshop pads the
 * last one; a last frame that stops at the span is accepted too).  A reset
 * group is decoded whole when it is at most 32 MiB, otherwise only its
 * first 32 MiB can be read.
 *
 * Records are the '/' entries, folders first, then files in section and
 * offset order, named without the leading '/'.
 */
typedef struct xx_chm {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t version;          /**< ITSF version, 2 or 3. */
    uint32_t sections;         /**< Content sections, section 0 included. */
    uint32_t lzx_sections;     /**< Sections that decode as LZX. */
    uint32_t chunk_size;       /**< Directory chunk size. */
    uint32_t listing_chunks;   /**< PMGL chunks. */
    uint64_t entries;          /**< All directory entries, "::" ones too. */
    uint64_t folders;          /**< Folder records. */
    uint64_t unsupported;      /**< File records that cannot be decoded. */
    int64_t content_offset;    /**< Content section 0, from base. */
    int64_t declared_size;     /**< File size in header section 0. */
} xx_chm;

typedef xx_chm xx_chm_t;

XXFC_API void xx_chm_init(xx_chm *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_chm *xx_chm_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_chm_destroy(xx_chm *archive);
XXFC_API void xx_chm_free(xx_chm *archive);

XXFC_API bool xx_chm_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_chm_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_chm_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_chm_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_chm_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_chm_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_chm_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_chm_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_chm_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CHM_H */
