/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_pea.h @brief PEA archive reader. */

#ifndef XXFCLIB_FORMAT_PEA_H
#define XXFCLIB_FORMAT_PEA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A PEA archive.
 *
 * PEA (Giorgio Tani's Pack, Encrypt, Authenticate, written by PeaZip's
 * pea.exe) is a layered container. All integers are little-endian unless
 * bit 0x80 of archive header byte 8 is set.
 *
 *   archive header, 10 bytes
 *     0      0xEA
 *     1      format version, 1
 *     2      format revision, 0..6
 *     3      volume control algorithm (a tag of that size ends every volume)
 *     4..9   writer information; byte 8 bit 0x80 selects big-endian
 *
 *   trigger object: u16 0, "POD\0"                       (offset 10)
 *   stream header
 *     +0     compression: 0 = stored, 1..3 = zlib (PCOMPRESS1..3)
 *     +1     0
 *     +2     stream control algorithm
 *     +3     object control algorithm
 *     +4     u32 block size, only when compression != 0 (1 MiB from pea)
 *
 *   object, repeated
 *     u16    name size (0 = a trigger object follows instead)
 *     name   UTF-8, the full source path ('\\' or '/'), a folder ends in one
 *     u32    DOS date/time
 *     u32    attributes; 0x10 = folder, which has no data part
 *     u64    size                                    (files only)
 *     data   stored: `size` raw bytes;
 *            compressed: per block of min(block size, remaining) bytes,
 *            u32 packed size then that many bytes (raw when packed size ==
 *            block length, else one zlib stream); when size != 0 a final u32
 *            repeats the last block's length
 *     tag    object control tag
 *
 *   trigger object: u16 0, "EOA\0"; then the stream tag, then the volume tag.
 *
 * Control algorithms and their tag sizes: 0x00 none (0), 0x01 Adler-32 (4),
 * 0x02 CRC-32 (4), 0x03 CRC-64 (8), 0x10 MD5 (16), 0x11 RIPEMD-160 (20),
 * 0x12 SHA-1 (20), 0x13 SHA-256 (32), 0x14 SHA-512 (64), 0x15 Whirlpool
 * (64), 0x16 SHA3-256 (32), 0x17 SHA3-512 (64), 0x18 BLAKE2s (32), 0x19
 * BLAKE2b (64). Stream algorithms 0x30..0x33 and 0x41..0x4C are
 * password-based (HMAC / EAX with AES, Twofish, Serpent): the archive is
 * identified but lists no members. The object tag covers the object header
 * through the size field, each packed-size field, the unpacked data and the
 * final u32; Adler-32 and CRC-32 tags are verified on extraction.
 *
 * Member names reproduce pea's EXTRACT2DIR mapping: a stored path is reduced
 * to its last component, and members under a stored folder keep their path
 * relative to that folder's parent.
 *
 * Only the file at hand is read: in a split set (NAME.000001.pea, ...) the
 * member that continues into the next volume is listed but not extracted.
 */
typedef struct xx_pea {
    Abstractformat format;
    uint8_t version;          /**< Archive header byte 2, 0..6. */
    uint8_t object_control;   /**< Archive header byte 3 (volume control). */
    uint8_t compression;      /**< First stream's compression, 0..3. */
    uint8_t stream_control;   /**< First stream's control byte. */
    bool big_endian;          /**< Set by bit 0x80 of archive header byte 8. */
    int64_t first_stream_offset; /**< Offset of the first "POD\0" trigger. */
    uint8_t member_control;   /**< Object control algorithm. */
    uint32_t block_size;      /**< Compression block size, 0 when stored. */
    uint64_t number_of_records;
    bool encrypted;           /**< The stream is password-protected. */
    bool complete;            /**< The "EOA\0" trigger was reached. */
} xx_pea;

typedef xx_pea xx_pea_t;
typedef xx_pea XPea;

XXFC_API void xx_pea_init(xx_pea *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_pea *xx_pea_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pea_destroy(xx_pea *archive);
XXFC_API void xx_pea_free(xx_pea *archive);

XXFC_API bool xx_pea_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pea_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pea_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_pea_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pea_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pea_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pea_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pea_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pea_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint8_t xx_pea_get_version(const xx_pea *archive);
XXFC_API uint8_t xx_pea_get_compression(const xx_pea *archive);
XXFC_API bool xx_pea_is_big_endian(const xx_pea *archive);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pea_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pea_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pea_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pea_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pea_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_PEA_H */
