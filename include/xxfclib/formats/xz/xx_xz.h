/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xz.h @brief Read-only standalone XZ stream reader. */

#ifndef XXFCLIB_FORMAT_XZ_H
#define XXFCLIB_FORMAT_XZ_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_xz xx_xz;
typedef struct xx_xz xx_xz_t;
typedef struct xx_xz XXz;

typedef enum xx_xz_data_struct_id_e {
    XX_XZ_DS_UNKNOWN = 0,
    XX_XZ_DS_STREAM_HEADER,
    XX_XZ_DS_BLOCK_HEADER,
    XX_XZ_DS_BLOCK_DATA,
    XX_XZ_DS_BLOCK_PADDING,
    XX_XZ_DS_BLOCK_CHECK,
    XX_XZ_DS_INDEX,
    XX_XZ_DS_STREAM_FOOTER,
    XX_XZ_DS_STREAM_PADDING
} xx_xz_data_struct_id_t;

struct xx_xz {
    Abstractformat format;       /**< Base format; must be first. */
    uint64_t number_of_blocks;
    uint64_t uncompressed_size;
    uint64_t compressed_data_size;
    int64_t index_offset;
    int64_t footer_offset;
    uint8_t check_type;
    bool can_extract;
    void *internal;
};

XXFC_API void xx_xz_init(xx_xz *xz, xx_io_device *dev,
                         int64_t base_address);
XXFC_API xx_xz *xx_xz_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_xz_free(xx_xz *xz);
XXFC_API void xx_xz_destroy(xx_xz *xz);

XXFC_API bool xx_xz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_xz_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_xz_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_xz_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode all concatenated XZ streams to a caller-provided device. */
XXFC_API bool xx_xz_unpack_to_device(xx_xz *xz, xx_io_device *destination,
                                     xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_xz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API const char *xx_xz_data_struct_id_to_string(Abstractformat *self,
                                                    uint32_t id);
XXFC_API uint32_t xx_xz_data_struct_string_to_id(Abstractformat *self,
                                                 const char *name);
XXFC_API xx_data_struct_state *xx_xz_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_xz_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_xz_data_struct_move_to_next(
    Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_xz_free_data_structs_reading(
    Abstractformat *self, xx_data_struct_state *state);

XXFC_API xx_data_struct_record_state *
xx_xz_create_data_struct_records_reading(Abstractformat *self,
                                         const xx_data_struct *ds,
                                         xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_xz_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_xz_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd);
XXFC_API void xx_xz_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint64_t xx_xz_get_number_of_blocks(const xx_xz *xz);
XXFC_API uint64_t xx_xz_get_uncompressed_size(const xx_xz *xz);
XXFC_API uint8_t xx_xz_get_check_type(const xx_xz *xz);
XXFC_API bool xx_xz_can_extract(const xx_xz *xz);

static inline Abstractformat *xx_xz_to_format(xx_xz *xz) {
    return xz ? &xz->format : NULL;
}

static inline const Abstractformat *xx_xz_to_format_const(const xx_xz *xz) {
    return xz ? &xz->format : NULL;
}

static inline void XXz_init(xx_xz *xz, xx_io_device *dev,
                            int64_t base_address) {
    xx_xz_init(xz, dev, base_address);
}

static inline xx_xz *XXz_create(xx_io_device *dev, int64_t base_address) {
    return xx_xz_create(dev, base_address);
}

static inline void XXz_free(xx_xz *xz) { xx_xz_free(xz); }

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XZ_H */
