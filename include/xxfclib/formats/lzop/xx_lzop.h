/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lzop.h @brief lzop (.lzo) single-stream container reader. */

/* lzop - the container the lzop(1) utility writes around LZO1X blocks.
 * Every scalar is BIG ENDIAN.
 *
 *   file header
 *     +0   9 bytes  89 4C 5A 4F 00 0D 0A 1A 0A  ("\x89LZO\0\r\n\x1a\n")
 *     +9   u16      version that wrote the file (0x0900 .. 0x1040)
 *     +11  u16      LZO library version
 *     ... version >= 0x0940 only:
 *          u16      version needed to extract
 *          u8       method (1 LZO1X-1, 2 LZO1X-999, 3 LZO1X-1(15))
 *          u8       level
 *     ... version <  0x0940: the method byte follows lib_version directly and
 *          there is no level byte.
 *          u32      flags
 *          u32      mode
 *          u32      mtime (low); version >= 0x0940 adds a second u32 (high)
 *          u8       file name length, then that many name bytes (optional)
 *          u32      header checksum over everything from "version" through
 *                   the name, adler32 unless F_H_CRC32 selects crc32
 *     ... flags & F_H_EXTRA_FIELD: u32 length, that many bytes, u32 checksum
 *
 *   block, repeated until a zero uncompressed length terminates the stream
 *     u32  uncompressed length; 0 is the end-of-stream marker
 *     u32  compressed length; equal to the uncompressed length means STORED
 *     u32  uncompressed checksum  (only if F_ADLER32_D / F_CRC32_D)
 *     u32  compressed checksum    (only if F_ADLER32_C / F_CRC32_C, and only
 *                                  when the block is really compressed)
 *     compressed length bytes of payload
 *
 * Several lzop streams may be concatenated in one file; each one repeats the
 * magic.  The block lengths are attacker controlled 32-bit fields, so the
 * scanner refuses an oversized uncompressed length at PARSE time rather than
 * discovering it while extracting: see XX_LZOPFMT_MAX_BLOCK_SIZE and
 * XX_LZOPFMT_MAX_TOTAL_OUTPUT in the implementation.
 *
 * The reader publishes exactly one archive record - the concatenation of every
 * decoded block - named after the stored file name when the header carries one.
 */

#ifndef XXFCLIB_FORMAT_LZOP_H
#define XXFCLIB_FORMAT_LZOP_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest stored file name an lzop header can express (u8 length). */
#define XX_LZOP_MAX_NAME_LENGTH 255U

typedef struct xx_lzop xx_lzop;
typedef struct xx_lzop xx_lzop_t;
typedef struct xx_lzop XLzop;

struct xx_lzop {
    Abstractformat format;
    uint64_t number_of_streams;    /**< Concatenated lzop streams found. */
    uint64_t number_of_blocks;     /**< Compressed blocks across all streams. */
    uint64_t uncompressed_size;    /**< Sum of every block's expanded length. */
    int64_t stream_end;            /**< base_address + format_size, or -1. */
    uint16_t version;              /**< Writer version from the first stream. */
    uint16_t library_version;      /**< LZO library version. */
    uint32_t flags;                /**< Header flags of the first stream. */
    uint8_t method;                /**< 1, 2 or 3. */
    uint8_t level;                 /**< 0 when the header predates 0x0940. */
    void *internal;
};

XXFC_API void xx_lzop_init(xx_lzop *archive, xx_io_device *dev,
                           int64_t base_address);
XXFC_API xx_lzop *xx_lzop_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_lzop_destroy(xx_lzop *archive);
XXFC_API void xx_lzop_free(xx_lzop *archive);

XXFC_API bool xx_lzop_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lzop_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_lzop_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_lzop_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzop_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzop_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzop_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzop_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzop_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Decode the whole container into destination.  Returns false unless every
 *  block of every stream decodes and its checksums verify. */
XXFC_API bool xx_lzop_unpack_to_device(xx_lzop *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd);

XXFC_API uint64_t xx_lzop_get_number_of_streams(const xx_lzop *archive);
XXFC_API uint64_t xx_lzop_get_number_of_blocks(const xx_lzop *archive);
XXFC_API uint64_t xx_lzop_get_uncompressed_size(const xx_lzop *archive);
XXFC_API int64_t xx_lzop_get_stream_end(const xx_lzop *archive);
XXFC_API uint32_t xx_lzop_get_flags(const xx_lzop *archive);
XXFC_API uint8_t xx_lzop_get_method(const xx_lzop *archive);
XXFC_API uint8_t xx_lzop_get_level(const xx_lzop *archive);
/** Stored file name, or NULL when the header carried none. */
XXFC_API const char *xx_lzop_get_stored_name(const xx_lzop *archive);

static inline Abstractformat *xx_lzop_to_format(xx_lzop *archive) {
    return archive ? &archive->format : NULL;
}
static inline void XLzop_init(xx_lzop *archive, xx_io_device *dev,
                              int64_t base_address) {
    xx_lzop_init(archive, dev, base_address);
}
static inline xx_lzop *XLzop_create(xx_io_device *dev, int64_t base_address) {
    return xx_lzop_create(dev, base_address);
}
static inline void XLzop_free(xx_lzop *archive) { xx_lzop_free(archive); }
static inline bool XLzop_is_valid(xx_lzop *archive, xx_pd_struct *pd) {
    return archive ? xx_format_is_valid(&archive->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZOP_H */
