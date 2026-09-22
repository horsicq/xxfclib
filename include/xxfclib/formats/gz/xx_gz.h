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
 * @file xx_gz.h
 * @brief RFC 1952 gzip stream reader. Gzip members are exposed independently;
 *        no tar/container interpretation is performed.
 */

#ifndef XXFCLIB_FORMAT_GZ_H
#define XXFCLIB_FORMAT_GZ_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_gz xx_gz;
typedef struct xx_gz xx_gz_t;
typedef struct xx_gz XGz;

typedef enum xx_gz_data_struct_id_e {
    XX_GZ_DS_UNKNOWN = 0,
    XX_GZ_DS_MEMBER_HEADER,
    XX_GZ_DS_EXTRA_FIELD,
    XX_GZ_DS_ORIGINAL_NAME,
    XX_GZ_DS_COMMENT,
    XX_GZ_DS_HEADER_CRC16,
    XX_GZ_DS_COMPRESSED_DATA,
    XX_GZ_DS_TRAILER
} xx_gz_data_struct_id_t;

struct xx_gz {
    Abstractformat format;       /**< Base format structure (must be first). */
    uint64_t number_of_members;  /**< Number of concatenated gzip members. */
    int64_t stream_end;          /**< Absolute end offset of the gzip stream. */
    void *internal;              /**< Parsed member table (private). */
};

XXFC_API void xx_gz_init(xx_gz *gz, xx_io_device *dev, int64_t base_address);
XXFC_API xx_gz *xx_gz_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_gz_free(xx_gz *gz);
XXFC_API void xx_gz_destroy(xx_gz *gz);

XXFC_API bool xx_gz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_gz_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_gz_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_gz_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

/** Decode all concatenated gzip members to a caller-provided device. */
XXFC_API bool xx_gz_unpack_to_device(xx_gz *gz, xx_io_device *destination,
                                     xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gz_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gz_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gz_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gz_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gz_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API const char *xx_gz_data_struct_id_to_string(Abstractformat *self, uint32_t id);
XXFC_API uint32_t xx_gz_data_struct_string_to_id(Abstractformat *self, const char *name);
XXFC_API xx_data_struct_state *xx_gz_create_data_structs_reading(Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_gz_get_current_data_struct(Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_gz_data_struct_move_to_next(Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_gz_free_data_structs_reading(Abstractformat *self, xx_data_struct_state *state);

XXFC_API xx_data_struct_record_state *xx_gz_create_data_struct_records_reading(Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_gz_get_current_data_struct_record(Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_gz_data_struct_record_move_to_next(Abstractformat *self, xx_data_struct_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gz_free_data_struct_records_reading(Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint64_t xx_gz_get_number_of_members(const xx_gz *gz);
XXFC_API int64_t xx_gz_get_stream_end(const xx_gz *gz);

static inline Abstractformat *xx_gz_to_format(xx_gz *gz) {
    return gz ? &gz->format : NULL;
}

static inline const Abstractformat *xx_gz_to_format_const(const xx_gz *gz) {
    return gz ? &gz->format : NULL;
}

static inline void XGz_init(xx_gz *gz, xx_io_device *dev, int64_t base_address) {
    xx_gz_init(gz, dev, base_address);
}

static inline xx_gz *XGz_create(xx_io_device *dev, int64_t base_address) {
    return xx_gz_create(dev, base_address);
}

static inline void XGz_free(xx_gz *gz) {
    xx_gz_free(gz);
}

static inline bool XGz_is_valid(xx_gz *gz, xx_pd_struct *pd) {
    return gz ? xx_format_is_valid(&gz->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GZ_H */
