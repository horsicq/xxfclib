/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_clickteam_multimedia_fusion.h
 *  @brief Clickteam Multimedia Fusion 2 / Fusion 2.5 stand-alone
 *         application (runtime pack in the PE overlay). */

#ifndef XXFCLIB_FORMAT_SFX_CLICKTEAM_MULTIMEDIA_FUSION_H
#define XXFCLIB_FORMAT_SFX_CLICKTEAM_MULTIMEDIA_FUSION_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Multimedia Fusion 2 / Fusion 2.5 stand-alone game executable.
 *
 * The runtime stub is an ordinary PE image.  Its overlay (the first byte
 * behind the last section's raw data) opens the runtime pack:
 *
 *   +0x00  u32 LE  0x77777777 ("wwww")
 *   +0x04  u32 LE  0x12478749
 *   +0x08  u32 LE  header size, 0x20
 *   +0x0C  u32 LE  size of the pack (not needed to walk it)
 *   +0x10  u32 LE  unknown
 *   +0x14  u32 LE  0
 *   +0x18  u32 LE  0
 *   +0x1C  u32 LE  number of packed runtime files (1..65536)
 *
 * followed by that many records
 *
 *   u16 LE  name length in characters (1..512)
 *   name    UTF-16LE, or 8-bit in older builds
 *   [u32 LE checksum]   only in the two-field layout; 0 means none
 *   u32 LE  packed size
 *   packed bytes: a zlib stream, except that some builds store the
 *                 first file (mmfs2.dll) as a plain MZ image
 *
 * The layout is not self-describing, so the first record is probed with
 * both name encodings and both size layouts, and exactly one reading has
 * to leave an MZ image or a zlib header behind the size fields; that
 * answer applies to the whole pack.  The checksum is the rotate-left-by-1
 * sum of the unpacked file taken as little-endian dwords, then of its
 * remaining tail bytes one at a time.
 *
 * Everything behind the last record, up to an Authenticode certificate
 * that ends the file, is the application's own ".ccn" chunk stream
 * ("PAMU"/"PAME"); it is published as the member "1.ccn".
 */
typedef struct xx_sfx_clickteam_multimedia_fusion {
    Abstractformat format;
    int64_t pack_offset;      /**< Device offset of the 0x20-byte header. */
    int64_t container_end;    /**< Device offset where the pack data ends. */
    int64_t ccn_offset;       /**< Device offset of "1.ccn", -1 if none. */
    int64_t ccn_size;
    uint32_t declared_files;  /**< The count at +0x1C. */
    uint32_t parsed_files;    /**< Complete records found in the table. */
    uint64_t number_of_records;
    bool unicode_names;
    bool two_size_fields;
    bool first_stored;        /**< The first record is a stored MZ image. */
    bool truncated;           /**< The table ended before its count. */
} xx_sfx_clickteam_multimedia_fusion;

typedef xx_sfx_clickteam_multimedia_fusion
    xx_sfx_clickteam_multimedia_fusion_t;

XXFC_API void xx_sfx_clickteam_multimedia_fusion_init(
    xx_sfx_clickteam_multimedia_fusion *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_sfx_clickteam_multimedia_fusion *
xx_sfx_clickteam_multimedia_fusion_create(xx_io_device *device,
                                          int64_t base_address);
XXFC_API void xx_sfx_clickteam_multimedia_fusion_destroy(
    xx_sfx_clickteam_multimedia_fusion *archive);
XXFC_API void xx_sfx_clickteam_multimedia_fusion_free(
    xx_sfx_clickteam_multimedia_fusion *archive);

XXFC_API bool xx_sfx_clickteam_multimedia_fusion_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sfx_clickteam_multimedia_fusion_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_clickteam_multimedia_fusion_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t
xx_sfx_clickteam_multimedia_fusion_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_sfx_clickteam_multimedia_fusion_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_sfx_clickteam_multimedia_fusion_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool
xx_sfx_clickteam_multimedia_fusion_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_clickteam_multimedia_fusion_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void
xx_sfx_clickteam_multimedia_fusion_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief The pack's per-file checksum over @p size bytes of @p data.
 *
 * Exposed for tests and tools; the reader verifies it on every record that
 * carries a non-zero value.
 */
XXFC_API uint32_t xx_sfx_clickteam_multimedia_fusion_checksum(
    const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_CLICKTEAM_MULTIMEDIA_FUSION_H */
