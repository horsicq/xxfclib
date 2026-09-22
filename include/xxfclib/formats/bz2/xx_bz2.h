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

typedef enum xx_bz2_data_struct_id_e {
    XX_BZ2_DS_UNKNOWN = 0,
    XX_BZ2_DS_STREAM_HEADER,
    XX_BZ2_DS_COMPRESSED_DATA
} xx_bz2_data_struct_id_t;

struct xx_bz2 {
    Abstractformat format;          /**< Base format structure (first member). */
    uint8_t block_size_100k;        /**< Header block-size multiplier (1..9). */
    uint64_t uncompressed_size;     /**< Size discovered while validating. */
    int64_t stream_end;             /**< Absolute end of the first stream. */
};

XXFC_API void xx_bz2_init(xx_bz2 *bz2, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_bz2 *xx_bz2_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_bz2_free(xx_bz2 *bz2);
XXFC_API void xx_bz2_destroy(xx_bz2 *bz2);

XXFC_API bool xx_bz2_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bz2_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bz2_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_bz2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode all directly concatenated bzip2 streams to a supplied device. */
XXFC_API bool xx_bz2_unpack_to_device(xx_bz2 *bz2,
                                      xx_io_device *destination,
                                      xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_bz2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bz2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_bz2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_bz2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_bz2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API const char *xx_bz2_data_struct_id_to_string(Abstractformat *self,
                                                      uint32_t id);
XXFC_API uint32_t xx_bz2_data_struct_string_to_id(Abstractformat *self,
                                                   const char *name);
XXFC_API xx_data_struct_state *xx_bz2_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_bz2_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_bz2_data_struct_move_to_next(
    Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_bz2_free_data_structs_reading(
    Abstractformat *self, xx_data_struct_state *state);

XXFC_API xx_data_struct_record_state *
xx_bz2_create_data_struct_records_reading(Abstractformat *self,
                                           const xx_data_struct *ds,
                                           xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_bz2_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_bz2_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd);
XXFC_API void xx_bz2_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint8_t xx_bz2_get_block_size_100k(const xx_bz2 *bz2);
XXFC_API uint64_t xx_bz2_get_uncompressed_size(const xx_bz2 *bz2);
XXFC_API int64_t xx_bz2_get_stream_end(const xx_bz2 *bz2);

static inline Abstractformat *xx_bz2_to_format(xx_bz2 *bz2) {
    return bz2 ? &bz2->format : NULL;
}

static inline const Abstractformat *xx_bz2_to_format_const(
    const xx_bz2 *bz2) {
    return bz2 ? &bz2->format : NULL;
}

static inline void XBz2_init(xx_bz2 *bz2, xx_io_device *dev,
                             int64_t base_address) {
    xx_bz2_init(bz2, dev, base_address);
}

static inline xx_bz2 *XBz2_create(xx_io_device *dev,
                                  int64_t base_address) {
    return xx_bz2_create(dev, base_address);
}

static inline void XBz2_free(xx_bz2 *bz2) {
    xx_bz2_free(bz2);
}

static inline bool XBz2_is_valid(xx_bz2 *bz2, xx_pd_struct *pd) {
    return bz2 ? xx_format_is_valid(&bz2->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BZ2_H */
