/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ewf_l01.h @brief EnCase logical evidence file (EWF-L01) reader. */

#ifndef XXFCLIB_FORMAT_EWF_L01_H
#define XXFCLIB_FORMAT_EWF_L01_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An EWF-L01 segment file (.L01, EnCase 5 to 7 "logical evidence").
 *
 * The container is the EWF-E01 section chain with a different signature:
 *
 *   file header (13 bytes)
 *     0x00  "LVF\x09\x0D\x0A\xFF\x00"
 *     0x08  u8     0x01 (start of fields)
 *     0x09  u16 LE segment number, 1 or higher
 *     0x0B  u16    0 (end of fields)
 *
 *   sections, each opened by a 76-byte descriptor:
 *     0x00  char[16] type ("header", "header2", "volume", "sectors",
 *                    "table", "table2", "ltree", "ltypes", "data",
 *                    "hash", "digest", "map", "next", "done", ...)
 *     0x10  u64 LE   next section offset, from the segment start
 *     0x18  u64 LE   section size, descriptor included
 *     0x48  u32 LE   Adler-32 of the 72 bytes before it
 *
 * The logical files are stored back to back in one "media" stream that is
 * cut into chunks exactly like an E01 disk image: the volume section gives
 * sectors per chunk and bytes per sector, the sectors section holds the
 * chunks (zlib, or plain bytes plus an Adler-32) and the table section
 * lists their offsets (bit 31 set: compressed) relative to a base offset.
 *
 * The ltree section (48-byte header: MD5, u64 data size, Adler-32 of the
 * header with its own checksum zeroed) holds a UTF-16LE text of tab
 * separated records. Its "entry" category is a pre-order tree: every entry
 * is a line "<0 or 26>\t<number of sub entries>" followed by a line of
 * values named by the category's type line. The values used here are
 * n (name), p (1: directory), ls (size), be (binary extents: hex count,
 * then per extent an optional "S", a hex media offset and a hex size),
 * opr (flags; 0x04000000 = sparse data) and du (duplicate data offset).
 *
 * Every file entry becomes one record named by its '/'-joined path below
 * the category root.
 */
typedef struct xx_ewf_l01 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_entries; /**< Files and directories in the ltree. */
    uint64_t media_size;        /**< Bytes the chunk tables can produce. */
    uint32_t number_of_chunks;
    uint32_t chunk_size;
    uint16_t segment_number;
    bool has_ltree;
    bool finished; /**< The chain ended in a "done" section. */
} xx_ewf_l01;

typedef xx_ewf_l01 xx_ewf_l01_t;

XXFC_API void xx_ewf_l01_init(xx_ewf_l01 *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_ewf_l01 *xx_ewf_l01_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_ewf_l01_destroy(xx_ewf_l01 *archive);
XXFC_API void xx_ewf_l01_free(xx_ewf_l01 *archive);

XXFC_API bool xx_ewf_l01_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_ewf_l01_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_ewf_l01_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_ewf_l01_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ewf_l01_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ewf_l01_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ewf_l01_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ewf_l01_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ewf_l01_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EWF_L01_H */
