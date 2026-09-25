/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ewf.h @brief Expert Witness Compression Format (EnCase E01 / SMART S01) reader. */

/* EWF version 1 - the segment files written by EnCase (.E01), FTK Imager,
 * ewfacquire and linen, and by ASR Data's SMART (.s01). All integers are
 * little endian; all Adler-32 values are zlib's.
 *
 *   file header (13 bytes)
 *     +0   char[8] "EVF" 09 0D 0A FF 00
 *     +8   u8      0x01
 *     +9   u16     segment number, 1 for .E01, 2 for .E02, ...
 *     +11  u16     0
 *
 *   section descriptor (76 bytes), the first one at +13
 *     +0   char[16] type, NUL padded ("header", "volume", "table", ...)
 *     +16  u64      offset of the next descriptor, from the segment start
 *     +24  u64      section size, descriptor included
 *     +32  byte[40] padding
 *     +72  u32      Adler-32 of bytes 0..71
 *   "next" (last section of a non-final segment) and "done" (last section
 *   of the set) point at themselves and carry a size of 0 or 76.
 *
 *   volume / disk / data section, EWF-E01 form (1052 bytes of data)
 *     +0   u8   media type        +4   u32 number of chunks
 *     +8   u32  sectors per chunk +12  u32 bytes per sector
 *     +16  u64  number of sectors +36  u8  media flags
 *     +52  u8   compression level +64  byte[16] set identifier
 *     +1048 u32 Adler-32 of bytes 0..1047
 *   EWF-S01 (SMART) form (94 bytes): +4 chunks, +8 sectors per chunk,
 *   +12 bytes per sector, +16 u32 number of sectors, +85 "SMART",
 *   +90 Adler-32 of bytes 0..89.
 *
 *   table / table2 section data
 *     +0   u32  number of entries
 *     +8   u64  base offset the entries are relative to (0 before EnCase 6.7)
 *     +20  u32  Adler-32 of bytes 0..19
 *     +24  u32  entries[n]: bit 31 set = zlib-compressed chunk, bits 0..30
 *               the chunk's offset from the base (the segment start when 0)
 *     then u32  Adler-32 of the entry array
 *   table2 is a copy of table. Chunk data lives in the "sectors" section
 *   that precedes the table (EnCase 2 and later), or inside the table
 *   section itself after the entry array (EnCase 1, SMART). A chunk runs to
 *   the next entry's offset; the last one of a table runs to the end of
 *   the section that holds it.
 *
 *   chunk: sectors_per_chunk * bytes_per_sector bytes of media (the final
 *   chunk only what is left), either a complete zlib stream, or the plain
 *   bytes followed by their Adler-32 as a little-endian u32.
 *
 *   hash section: MD5 of the media at +0; digest section: MD5 at +0 and
 *   SHA-1 at +16.
 *
 * The reader publishes ONE member, disk.img, the acquired media, and
 * verifies every chunk checksum and, when the image records one, the MD5.
 * A set split over several segment files is read when the device presents
 * the segments back to back (a concatenation, or a multi-volume device):
 * segment N+1 must start right after segment N's "next" section. A lone
 * .E01 of a larger set is recognised and listed, but its extraction fails
 * because the chunks of the other segments are not there.
 *
 * Not handled: EWF version 2 (Ex01, "EVF2"), logical evidence files
 * (L01, "LVF"), delta segments (.d01) and encrypted images.
 */

#ifndef XXFCLIB_FORMAT_EWF_H
#define XXFCLIB_FORMAT_EWF_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ewf xx_ewf;
typedef struct xx_ewf xx_ewf_t;
typedef struct xx_ewf XEwf;

struct xx_ewf {
    Abstractformat format;
    uint64_t number_of_records;   /**< Always 1: the media image. */
    uint64_t media_size;          /**< number_of_sectors * bytes_per_sector. */
    uint64_t number_of_sectors;
    uint64_t table_entries;       /**< Chunk entries found in the device. */
    uint32_t number_of_chunks;    /**< As the volume section declares it. */
    uint32_t sectors_per_chunk;
    uint32_t bytes_per_sector;
    uint32_t chunk_size;
    uint32_t segment_count;       /**< Segments found back to back. */
    uint16_t first_segment;       /**< Segment number of the first one. */
    uint8_t media_type;
    uint8_t media_flags;
    uint8_t compression_level;
    bool is_smart;                /**< 94-byte EWF-S01 volume section. */
    bool is_complete;             /**< Every chunk of the media is present. */
    bool has_md5;
    bool has_sha1;
    uint8_t md5[16];
    uint8_t sha1[20];
    void *internal;
};

XXFC_API void xx_ewf_init(xx_ewf *ewf, xx_io_device *dev, int64_t base_address);
XXFC_API xx_ewf *xx_ewf_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ewf_destroy(xx_ewf *ewf);
XXFC_API void xx_ewf_free(xx_ewf *ewf);

XXFC_API bool xx_ewf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ewf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_ewf_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_ewf_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ewf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ewf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ewf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ewf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ewf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Write the media image to @p output (NULL only decodes and verifies).
 *
 * Fails when a chunk is missing, damaged or fails its checksum, or when the
 * image records an MD5 that the media does not match.
 */
XXFC_API bool xx_ewf_unpack_to_device(xx_ewf *ewf, xx_io_device *output,
                                      xx_pd_struct *pd);

XXFC_API uint64_t xx_ewf_get_media_size(const xx_ewf *ewf);
XXFC_API uint32_t xx_ewf_get_chunk_size(const xx_ewf *ewf);
XXFC_API uint32_t xx_ewf_get_bytes_per_sector(const xx_ewf *ewf);
XXFC_API bool xx_ewf_is_complete(const xx_ewf *ewf);

static inline Abstractformat *xx_ewf_to_format(xx_ewf *ewf) {
    return ewf ? &ewf->format : NULL;
}
static inline void XEwf_init(xx_ewf *ewf, xx_io_device *dev,
                             int64_t base_address) {
    xx_ewf_init(ewf, dev, base_address);
}
static inline xx_ewf *XEwf_create(xx_io_device *dev, int64_t base_address) {
    return xx_ewf_create(dev, base_address);
}
static inline void XEwf_free(xx_ewf *ewf) { xx_ewf_free(ewf); }
static inline bool XEwf_is_valid(xx_ewf *ewf, xx_pd_struct *pd) {
    return ewf ? xx_format_is_valid(&ewf->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EWF_H */
