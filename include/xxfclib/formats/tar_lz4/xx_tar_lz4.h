/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tar_lz4.h @brief LZ4-frame compressed TAR archive reader/writer. */

#ifndef XXFCLIB_FORMAT_TAR_LZ4_H
#define XXFCLIB_FORMAT_TAR_LZ4_H

#include "xxfclib/formats/tar/xx_tar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tar_lz4 xx_tar_lz4;
typedef struct xx_tar_lz4 xx_tar_lz4_t;
typedef struct xx_tar_lz4 XTarLz4;

typedef enum xx_tar_lz4_data_struct_id_e {
    XX_TAR_LZ4_DS_UNKNOWN = 0,
    XX_TAR_LZ4_DS_TAR
} xx_tar_lz4_data_struct_id_t;

struct xx_tar_lz4 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    int64_t compressed_size;
    int64_t uncompressed_size;
    void *internal;
};

XXFC_API void xx_tar_lz4_init(xx_tar_lz4 *tar_lz4, xx_io_device *dev,
                               int64_t base_address);
XXFC_API xx_tar_lz4 *xx_tar_lz4_create(xx_io_device *dev,
                                        int64_t base_address);
XXFC_API void xx_tar_lz4_destroy(xx_tar_lz4 *tar_lz4);
XXFC_API void xx_tar_lz4_free(xx_tar_lz4 *tar_lz4);

XXFC_API bool xx_tar_lz4_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_tar_lz4_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_tar_lz4_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_tar_lz4_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tar_lz4_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tar_lz4_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tar_lz4_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tar_lz4_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_lz4_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API xx_archive_write_state *xx_tar_lz4_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_tar_lz4_pack_archive_record(
    Abstractformat *self, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd);
XXFC_API bool xx_tar_lz4_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_lz4_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state);

XXFC_API const char *xx_tar_lz4_data_struct_id_to_string(
    Abstractformat *self, uint32_t id);
XXFC_API uint32_t xx_tar_lz4_data_struct_string_to_id(
    Abstractformat *self, const char *name);
XXFC_API xx_data_struct_state *xx_tar_lz4_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_tar_lz4_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_tar_lz4_data_struct_move_to_next(
    Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_lz4_free_data_structs_reading(
    Abstractformat *self, xx_data_struct_state *state);
XXFC_API xx_data_struct_record_state *
xx_tar_lz4_create_data_struct_records_reading(
    Abstractformat *self, const xx_data_struct *ds, xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_tar_lz4_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_tar_lz4_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd);
XXFC_API void xx_tar_lz4_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint64_t xx_tar_lz4_get_number_of_records(
    const xx_tar_lz4 *tar_lz4);
XXFC_API uint64_t xx_tar_lz4_get_number_of_members(
    const xx_tar_lz4 *tar_lz4);
XXFC_API int64_t xx_tar_lz4_get_compressed_size(const xx_tar_lz4 *tar_lz4);
XXFC_API int64_t xx_tar_lz4_get_uncompressed_size(
    const xx_tar_lz4 *tar_lz4);

static inline Abstractformat *xx_tar_lz4_to_format(xx_tar_lz4 *tar_lz4) {
    return tar_lz4 ? &tar_lz4->format : NULL;
}

static inline void XTarLz4_init(xx_tar_lz4 *tar_lz4, xx_io_device *dev,
                                int64_t base_address) {
    xx_tar_lz4_init(tar_lz4, dev, base_address);
}
static inline xx_tar_lz4 *XTarLz4_create(xx_io_device *dev,
                                          int64_t base_address) {
    return xx_tar_lz4_create(dev, base_address);
}
static inline void XTarLz4_free(xx_tar_lz4 *tar_lz4) {
    xx_tar_lz4_free(tar_lz4);
}
static inline bool XTarLz4_is_valid(xx_tar_lz4 *tar_lz4, xx_pd_struct *pd) {
    return tar_lz4 ? xx_format_is_valid(&tar_lz4->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TAR_LZ4_H */
