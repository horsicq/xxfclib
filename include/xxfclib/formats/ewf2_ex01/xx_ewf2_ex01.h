/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ewf2_ex01.h @brief Expert Witness Compression Format 2 (EnCase 7+ .Ex01) reader. */

/* EWF version 2, "EnCase Evidence File Format Version 2": the .Ex01 segment
 * files written by EnCase 7 and later and by FTK Imager 4+. All integers are
 * little endian; all Adler-32 values are zlib's.
 *
 *   file header (32 bytes)
 *     +0   char[8] "EVF2" 0D 0A 81 00
 *     +8   u8      major version, 2
 *     +9   u8      minor version, 1
 *     +10  u16     compression method: 0 none, 1 zlib, 2 bzip2
 *     +12  u32     segment number, 1 for .Ex01
 *     +16  byte[16] set identifier (GUID)
 *
 *   A section is its data, padded to 16 bytes, FOLLOWED by a 64-byte
 *   descriptor. Descriptors point backwards, so the chain is read from the
 *   last section of the segment (a "next" or "done" section with no data).
 *
 *   section descriptor (64 bytes)
 *     +0   u32  type    1 device information, 2 case data, 3 sector data,
 *                       4 sector table, 5 error table, 6 session table,
 *                       8 MD5, 9 SHA1, 0x0B encryption keys, 0x0D next,
 *                       0x0F done, ...
 *     +4   u32  data flags: 1 integrity MD5 set, 2 data encrypted
 *     +8   u64  offset of the previous section's descriptor from the
 *               segment start, 0 for the first section (whose data starts
 *               at +32); a section's data starts right after the previous
 *               descriptor
 *     +16  u64  data size, padding included
 *     +24  u32  descriptor size, 64
 *     +28  u32  padding size
 *     +32  byte[16] MD5 of the data (when flag 1)
 *     +60  u32  Adler-32 of bytes 0..59
 *
 *   device information / case data: a serialized object string, UTF-16
 *   with a byte-order mark, compressed with the file header's method.
 *   Line 3 holds tab-separated tags and line 4 the matching values; the
 *   reader uses ts (number of sectors) and bp (bytes per sector) from the
 *   device information, sb (sectors per chunk) and tb (chunks) from the
 *   case data.
 *
 *   sector table data
 *     +0   u64  first chunk number     +8  u32 number of entries
 *     +16  u32  Adler-32 of bytes 0..15, then 12 bytes of padding
 *     +32  entries[n], 16 bytes each:
 *            +0 u64 chunk offset from the segment start (or an 8-byte fill
 *                   pattern), +8 u32 stored size, +12 u32 flags:
 *                   1 compressed, 2 followed by its Adler-32, 4 pattern fill
 *                   (only together with 1)
 *     then u32  Adler-32 of the entry array, 12 bytes of padding
 *   MD5 section: 16-byte MD5 of the media + its Adler-32; SHA1 section:
 *   20-byte SHA-1 + its Adler-32.
 *
 * The reader publishes ONE member, disk.img, the acquired media, and checks
 * every chunk (Adler-32 or the codec's own trailer) and, when the image
 * records them, the MD5 and SHA-1 of the whole media.
 *
 * A segment followed by bytes that are not part of it (a carved or padded
 * file) is found by scanning forward for each descriptor instead; the
 * format size then ends at the last descriptor.
 *
 * Not handled: encrypted images (listed, not extracted), logical evidence
 * files (Lx01, "LEF2"), and sets split over several segment files (a lone
 * segment is listed; its extraction fails unless it holds every chunk).
 */

#ifndef XXFCLIB_FORMAT_EWF2_EX01_H
#define XXFCLIB_FORMAT_EWF2_EX01_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ewf2_ex01 xx_ewf2_ex01;
typedef struct xx_ewf2_ex01 xx_ewf2_ex01_t;
typedef struct xx_ewf2_ex01 XEwf2Ex01;

struct xx_ewf2_ex01 {
    Abstractformat format;
    uint64_t number_of_records;   /**< Always 1: the media image. */
    uint64_t media_size;          /**< number_of_sectors * bytes_per_sector. */
    uint64_t number_of_sectors;
    uint64_t number_of_chunks;
    uint64_t table_entries;       /**< Chunk entries found in the segment. */
    uint32_t sectors_per_chunk;
    uint32_t bytes_per_sector;
    uint32_t chunk_size;
    uint32_t segment_number;
    uint16_t compression_method;  /**< 0 none, 1 zlib, 2 bzip2. */
    uint8_t minor_version;
    bool has_geometry;
    bool is_encrypted;
    bool is_last_segment;         /**< The chain ends in a "done" section. */
    bool is_complete;             /**< Every chunk of the media is present. */
    bool has_md5;
    bool has_sha1;
    uint8_t md5[16];
    uint8_t sha1[20];
    uint8_t set_identifier[16];
    void *internal;
};

XXFC_API void xx_ewf2_ex01_init(xx_ewf2_ex01 *ewf, xx_io_device *dev,
                                int64_t base_address);
XXFC_API xx_ewf2_ex01 *xx_ewf2_ex01_create(xx_io_device *dev,
                                           int64_t base_address);
XXFC_API void xx_ewf2_ex01_destroy(xx_ewf2_ex01 *ewf);
XXFC_API void xx_ewf2_ex01_free(xx_ewf2_ex01 *ewf);

XXFC_API bool xx_ewf2_ex01_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_ewf2_ex01_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_ewf2_ex01_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_ewf2_ex01_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ewf2_ex01_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ewf2_ex01_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ewf2_ex01_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ewf2_ex01_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ewf2_ex01_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Write the media image to @p output (NULL only decodes and verifies).
 *
 * Fails when a chunk is missing, damaged or fails its checksum, when the
 * image is encrypted, or when a recorded MD5 / SHA-1 does not match.
 */
XXFC_API bool xx_ewf2_ex01_unpack_to_device(xx_ewf2_ex01 *ewf,
                                            xx_io_device *output,
                                            xx_pd_struct *pd);

XXFC_API uint64_t xx_ewf2_ex01_get_media_size(const xx_ewf2_ex01 *ewf);
XXFC_API uint32_t xx_ewf2_ex01_get_chunk_size(const xx_ewf2_ex01 *ewf);
XXFC_API bool xx_ewf2_ex01_is_complete(const xx_ewf2_ex01 *ewf);

static inline Abstractformat *xx_ewf2_ex01_to_format(xx_ewf2_ex01 *ewf) {
    return ewf ? &ewf->format : NULL;
}
static inline void XEwf2Ex01_init(xx_ewf2_ex01 *ewf, xx_io_device *dev,
                                  int64_t base_address) {
    xx_ewf2_ex01_init(ewf, dev, base_address);
}
static inline xx_ewf2_ex01 *XEwf2Ex01_create(xx_io_device *dev,
                                             int64_t base_address) {
    return xx_ewf2_ex01_create(dev, base_address);
}
static inline void XEwf2Ex01_free(xx_ewf2_ex01 *ewf) {
    xx_ewf2_ex01_free(ewf);
}
static inline bool XEwf2Ex01_is_valid(xx_ewf2_ex01 *ewf, xx_pd_struct *pd) {
    return ewf ? xx_format_is_valid(&ewf->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_EWF2_EX01_H */
