/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ewf2_lx01.h @brief EnCase 7+ logical evidence file (EWF2-Lx01) reader. */

/* EWF version 2, logical flavour: the .Lx01 segment files EnCase 7 and
 * later write when files (not a disk) are acquired. All integers are little
 * endian; every Adler-32 is zlib's.
 *
 *   file header (32 bytes)
 *     +0   char[8]  "LEF2" 0D 0A 81 00
 *     +8   u8       major version 2
 *     +9   u8       minor version 1
 *     +10  u16      compression method: 0 none, 1 zlib, 2 bzip2
 *     +12  u32      segment number (1 for .Lx01)
 *     +16  byte[16] segment set GUID
 *
 *   sections follow the header. Each section is its data (padded to 16
 *   bytes) and then a 64-byte descriptor, so the chain is read from the END
 *   of the segment backwards:
 *     +0   u32  type             +4   u32 flags (1 = MD5 set, 2 = encrypted)
 *     +8   u64  offset of the previous descriptor (0: this is the first)
 *     +16  u64  data size, padding included
 *     +24  u32  descriptor size (64)   +28 u32 padding size
 *     +32  byte[16] MD5 of the data     +48 byte[12] zero
 *     +60  u32  Adler-32 of bytes 0..59
 *   A section's data starts right after the previous descriptor (at +32 for
 *   the first). The last section is "done" (0x0F, last segment) or "next"
 *   (0x0D, more segments follow).
 *
 *   used section types
 *     0x01 device information, 0x02 case data: a zlib (header method)
 *          compressed UTF-16 text "1\nmain\n<tags>\n<values>\n\n"; "bp" in
 *          the device information is the bytes per sector, "sb" in the case
 *          data the sectors per chunk.
 *     0x03 sector data: the chunks.
 *     0x04 sector table: u64 first chunk, u32 entries, u32 0, u32 Adler-32 of
 *          bytes 0..15, 12 zero bytes; then entries of {u64 offset from the
 *          segment start, u32 size, u32 flags (1 compressed, 2 plain + Adler
 *          trailer, 4 with 1: the offset field is a 64-bit fill pattern)};
 *          then u32 Adler-32 of the entry array and 12 zero bytes.
 *     0x0B encryption keys (the reader then lists nothing).
 *     0x20 single files data: the UTF-16LE "ltree" text of EWF-L01 - five
 *          categories (rec, perm, srce, sub, entry). The entry category is a
 *          tree of "<a>\t<children>" / tab separated value lines whose
 *          columns are named by its type line: p (1 = directory), n (name),
 *          ls (size), be (extents "count [S] offset size ..." in hex, offsets
 *          into the media), opr (flags; 0x04000000 = one byte repeated),
 *          du (duplicate data offset), ha (MD5 of the file data).
 *
 *   The media is the concatenation of the chunks in chunk-number order,
 *   each sectors_per_chunk * bytes_per_sector long (the final one may be
 *   shorter). A file's data is the bytes its extents select from the media.
 *
 * The reader publishes one record per file below the tree root, named by
 * the '/' joined entry names under the root (characters Windows forbids,
 * controls and '%' are written as %XX). Every chunk checksum is verified,
 * and a file's MD5 when the entry carries one.
 *
 * Not handled: encrypted evidence, bzip2 compressed chunks, and data that
 * lives in other segment files of a multi-segment set (such members fail).
 */

#ifndef XXFCLIB_FORMAT_EWF2_LX01_H
#define XXFCLIB_FORMAT_EWF2_LX01_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ewf2_lx01 {
    Abstractformat format;
    uint64_t number_of_records; /**< Files below the ltree root. */
    uint64_t number_of_entries; /**< Every ltree entry, root included. */
    uint64_t number_of_chunks;  /**< Chunk entries of all sector tables. */
    uint64_t media_size;        /**< "tb" of the rec category, 0 if absent. */
    uint32_t segment_number;
    uint32_t number_of_sections;
    uint32_t sectors_per_chunk;
    uint32_t bytes_per_sector;
    uint32_t chunk_size;
    uint16_t compression_method;
    bool is_last_segment; /**< Chain ends in "done", not "next". */
    bool is_encrypted;
    bool has_single_files; /**< A readable single files data section. */
} xx_ewf2_lx01;

typedef xx_ewf2_lx01 xx_ewf2_lx01_t;

XXFC_API void xx_ewf2_lx01_init(xx_ewf2_lx01 *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_ewf2_lx01 *xx_ewf2_lx01_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_ewf2_lx01_destroy(xx_ewf2_lx01 *archive);
XXFC_API void xx_ewf2_lx01_free(xx_ewf2_lx01 *archive);

XXFC_API bool xx_ewf2_lx01_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ewf2_lx01_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ewf2_lx01_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ewf2_lx01_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ewf2_lx01_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ewf2_lx01_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ewf2_lx01_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ewf2_lx01_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ewf2_lx01_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EWF2_LX01_H */
