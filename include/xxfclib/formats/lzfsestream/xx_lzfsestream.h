/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lzfsestream.h @brief Standalone LZFSE / LZVN stream reader. */

/* A standalone LZFSE stream is what Apple's `lzfse -encode` and
 * `compression_encode_buffer(COMPRESSION_LZFSE)` write: a run of blocks, each
 * opening with a four byte little endian magic, closed by an end-of-stream
 * marker.  There is no file header, no checksum and no stored name.
 *
 *   bvx-  u32 n_raw_bytes, then n_raw_bytes stored verbatim            (8 + n)
 *   bvxn  u32 n_raw_bytes, u32 n_payload_bytes, then an LZVN payload   (12 + n)
 *   bvx2  u32 n_raw_bytes, three packed u64; the low 32 bits of the third
 *         are header_size, then bit-packed frequency tables up to
 *         header_size, then n_literal_payload_bytes (u64 #0, bits 20..39)
 *         and n_lmd_payload_bytes (u64 #1, bits 40..59)
 *   bvx1  the same fields unpacked into the reference's C struct
 *         lzfse_compressed_block_header_v1, 772 bytes, then the two payloads
 *   bvx$  end of stream, the four magic bytes only
 *
 * The reference encoder writes a single bvx- block for incompressible or
 * empty input (the empty stream is "bvx-" 00000000 "bvx$", twelve bytes), a
 * single bvxn block for input under 4 KiB, and bvx2 blocks otherwise.  bvx1
 * is decoded but no longer produced.
 *
 * Source: binwalk's src/signatures/lzfse.rs (the four opening magics and the
 * block walk that must end at bvx$) and src/structures/lzfse.rs (the per
 * block sizes).  The reader's format size is binwalk's carve length: from the
 * first block to the end of the bvx$ marker; anything after it is overlay.
 *
 * One deliberate difference from binwalk: binwalk skips 770 bytes of bvx1
 * header, which is the unpadded sum of the struct's fields, but the reference
 * decoder copies and skips sizeof(lzfse_compressed_block_header_v1), which is
 * 772 because the struct is four byte aligned.  A genuine bvx1 block is
 * therefore walked here with 772, as src/algo/lzfse decodes it; binwalk lands
 * two bytes short of the next magic on such a stream and reports nothing.
 *
 * Beyond the walk, validation decodes the whole stream with src/algo/lzfse
 * into a buffer of exactly the sum of the blocks' n_raw_bytes, and every
 * block must produce exactly its declared count.  The one archive record is
 * the decoded data.
 */

#ifndef XXFCLIB_FORMAT_LZFSESTREAM_H
#define XXFCLIB_FORMAT_LZFSESTREAM_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Block kinds, as bits of xx_lzfsestream::block_types. */
#define XX_LZFSESTREAM_BLOCK_RAW 0x01U  /**< bvx- */
#define XX_LZFSESTREAM_BLOCK_V1 0x02U   /**< bvx1 */
#define XX_LZFSESTREAM_BLOCK_V2 0x04U   /**< bvx2 */
#define XX_LZFSESTREAM_BLOCK_LZVN 0x08U /**< bvxn */

/** @brief The smallest stream: an empty bvx- block and the bvx$ marker. */
#define XX_LZFSESTREAM_MIN_SIZE 12U

typedef struct xx_lzfsestream xx_lzfsestream;
typedef struct xx_lzfsestream xx_lzfsestream_t;
typedef struct xx_lzfsestream XLzfsestream;

struct xx_lzfsestream {
    Abstractformat format;
    uint64_t uncompressed_size; /**< Sum of every block's n_raw_bytes. */
    int64_t stream_end;         /**< Offset just past bvx$, or -1. */
    uint32_t number_of_blocks;  /**< Data blocks, not counting bvx$. */
    uint32_t block_types;       /**< XX_LZFSESTREAM_BLOCK_* seen. */
};

XXFC_API void xx_lzfsestream_init(xx_lzfsestream *archive,
                                  xx_io_device *device, int64_t base_address);
XXFC_API xx_lzfsestream *xx_lzfsestream_create(xx_io_device *device,
                                               int64_t base_address);
XXFC_API void xx_lzfsestream_destroy(xx_lzfsestream *archive);
XXFC_API void xx_lzfsestream_free(xx_lzfsestream *archive);

XXFC_API bool xx_lzfsestream_check_is_valid(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API bool xx_lzfsestream_handle_base_info(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API int64_t xx_lzfsestream_get_format_size(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API uint64_t xx_lzfsestream_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the stream to a caller-provided device. */
XXFC_API bool xx_lzfsestream_unpack_to_device(xx_lzfsestream *archive,
                                              xx_io_device *destination,
                                              xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lzfsestream_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lzfsestream_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lzfsestream_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lzfsestream_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lzfsestream_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_lzfsestream_get_uncompressed_size(
    const xx_lzfsestream *archive);
XXFC_API int64_t xx_lzfsestream_get_stream_end(const xx_lzfsestream *archive);
XXFC_API uint32_t xx_lzfsestream_get_number_of_blocks(
    const xx_lzfsestream *archive);
XXFC_API uint32_t xx_lzfsestream_get_block_types(
    const xx_lzfsestream *archive);

static inline Abstractformat *xx_lzfsestream_to_format(
    xx_lzfsestream *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LZFSESTREAM_H */
