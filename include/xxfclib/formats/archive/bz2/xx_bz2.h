/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_bz2.h
 * @brief Read-only bzip2 stream format reader.
 */

#ifndef XXFCLIB_FORMAT_BZ2_H
#define XXFCLIB_FORMAT_BZ2_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_bz2 xx_bz2;
typedef struct xx_bz2 xx_bz2_t;
typedef struct xx_bz2 XBz2;

/* The ids are defined by src/datastructs/bz2.xxds and must match it. */
typedef enum xx_bz2_data_struct_id_e {
    XX_BZ2_DS_UNKNOWN = 0,
    XX_BZ2_DS_STREAM_HEADER,
    XX_BZ2_DS_COMPRESSED_DATA
} xx_bz2_data_struct_id_t;

struct xx_bz2 {
    Abstractformat format;      /**< Base format structure (first member). */
    uint8_t block_size_100k;    /**< Header block-size multiplier (1..9). */
    uint64_t uncompressed_size; /**< Size discovered while validating. */
    int64_t stream_end;         /**< Absolute end of the first stream. */
};

XXFC_API void xx_bz2_init(xx_bz2 *bz2, xx_io_device *dev, int64_t base_address);
XXFC_API xx_bz2 *xx_bz2_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_bz2_free(xx_bz2 *bz2);
XXFC_API void xx_bz2_destroy(xx_bz2 *bz2);

XXFC_API bool xx_bz2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bz2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bz2_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_bz2_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

/** Decode all directly concatenated bzip2 streams to a supplied device. */
XXFC_API bool xx_bz2_unpack_to_device(xx_bz2 *bz2, xx_io_device *destination, xx_pd_struct *pd);
/** Decode at most `capacity` bytes from the start of the streams at
 * `base_address` into `buffer`, without decoding past them, for a cheap look
 * at what they hold. True with the bytes in `*size` when the buffer filled or
 * the streams ended; `*complete` (optional) is true in the second case, in
 * which they were verified as xx_bz2_handle_base_info does. False on
 * malformed data in that range, a read error or a stop. */
XXFC_API bool xx_bz2_decode_prefix(xx_io_device *device, int64_t base_address, void *buffer, size_t capacity, size_t *size, bool *complete, xx_pd_struct *pd);
/** Decode the streams at `base_address` into `destination` in one pass,
 * verified as xx_bz2_handle_base_info does; true with their extent (as
 * xx_bz2_get_format_size gives it) in `*stream_size`. When the file ends
 * inside a stream, false with `*cut` (optional) set: the destination then
 * holds the blocks before the cut one, each checked by its CRC. A damaged
 * stream fails with it clear. */
XXFC_API bool xx_bz2_decode_stream(xx_io_device *device, int64_t base_address, xx_io_device *destination, int64_t *stream_size, bool *cut, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bz2_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bz2_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bz2_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bz2_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bz2_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/** Write exactly one regular file (including an empty file) through borrowed
 * devices. Compression level and memory/member limits are supported. Multiple
 * members, directories, links and encryption are rejected. The source cursor
 * is restored, and no temporary files are created. */
XXFC_API xx_archive_write_state *xx_bz2_create_archive_records_writing(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_bz2_pack_archive_record(Abstractformat *self, xx_archive_write_state *state, const xx_archive_record *record, xx_io_device *source, xx_pd_struct *pd);
XXFC_API bool xx_bz2_finalize_archive_records_writing(Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
XXFC_API void xx_bz2_free_archive_records_writing(Abstractformat *self, xx_archive_write_state *state);

XXFC_API uint8_t xx_bz2_get_block_size_100k(const xx_bz2 *bz2);
XXFC_API uint64_t xx_bz2_get_uncompressed_size(const xx_bz2 *bz2);
XXFC_API int64_t xx_bz2_get_stream_end(const xx_bz2 *bz2);

static inline Abstractformat *xx_bz2_to_format(xx_bz2 *bz2)
{
    return bz2 ? &bz2->format : NULL;
}

static inline const Abstractformat *xx_bz2_to_format_const(const xx_bz2 *bz2)
{
    return bz2 ? &bz2->format : NULL;
}

static inline void XBz2_init(xx_bz2 *bz2, xx_io_device *dev, int64_t base_address)
{
    xx_bz2_init(bz2, dev, base_address);
}

static inline xx_bz2 *XBz2_create(xx_io_device *dev, int64_t base_address)
{
    return xx_bz2_create(dev, base_address);
}

static inline void XBz2_free(xx_bz2 *bz2)
{
    xx_bz2_free(bz2);
}

static inline bool XBz2_is_valid(xx_bz2 *bz2, xx_pd_struct *pd)
{
    return bz2 ? xx_format_is_valid(&bz2->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_bz2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_bz2_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_bz2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_bz2_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_bz2_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_BZ2_H */
