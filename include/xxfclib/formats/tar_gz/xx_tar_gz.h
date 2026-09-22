/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tar_gz.h @brief Gzip-compressed TAR archive reader and writer. */

#ifndef XXFCLIB_FORMAT_TAR_GZ_H
#define XXFCLIB_FORMAT_TAR_GZ_H

#include "xxfclib/formats/tar/xx_tar.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tar_gz xx_tar_gz;
typedef struct xx_tar_gz xx_tar_gz_t;
typedef struct xx_tar_gz XTarGz;

struct xx_tar_gz {
    Abstractformat format;       /**< Base format; must be first. */
    uint64_t number_of_records;  /**< Visible TAR archive entries. */
    uint64_t number_of_members;  /**< Physical TAR headers. */
    int64_t compressed_size;     /**< Size of the gzip transport. */
    int64_t uncompressed_size;   /**< Size of the materialized TAR image. */
    void *internal;              /**< Private compressed-TAR context. */
};

XXFC_API void xx_tar_gz_init(xx_tar_gz *tar_gz, xx_io_device *dev,
                             int64_t base_address);
XXFC_API xx_tar_gz *xx_tar_gz_create(xx_io_device *dev,
                                     int64_t base_address);
XXFC_API void xx_tar_gz_free(xx_tar_gz *tar_gz);
XXFC_API void xx_tar_gz_destroy(xx_tar_gz *tar_gz);

XXFC_API bool xx_tar_gz_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_tar_gz_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_tar_gz_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_tar_gz_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tar_gz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tar_gz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tar_gz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tar_gz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_gz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API xx_archive_write_state *xx_tar_gz_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_tar_gz_pack_archive_record(
    Abstractformat *self, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd);
XXFC_API bool xx_tar_gz_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_gz_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state);

XXFC_API const char *xx_tar_gz_data_struct_id_to_string(Abstractformat *self,
                                                        uint32_t id);
XXFC_API uint32_t xx_tar_gz_data_struct_string_to_id(Abstractformat *self,
                                                     const char *name);
XXFC_API xx_data_struct_state *xx_tar_gz_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_tar_gz_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_tar_gz_data_struct_move_to_next(
    Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_gz_free_data_structs_reading(
    Abstractformat *self, xx_data_struct_state *state);

XXFC_API xx_data_struct_record_state *
xx_tar_gz_create_data_struct_records_reading(Abstractformat *self,
                                              const xx_data_struct *ds,
                                              xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *
xx_tar_gz_get_current_data_struct_record(Abstractformat *self,
                                         xx_data_struct_record_state *state);
XXFC_API bool xx_tar_gz_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd);
XXFC_API void xx_tar_gz_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint64_t xx_tar_gz_get_number_of_records(const xx_tar_gz *tar_gz);
XXFC_API uint64_t xx_tar_gz_get_number_of_members(const xx_tar_gz *tar_gz);
XXFC_API int64_t xx_tar_gz_get_compressed_size(const xx_tar_gz *tar_gz);
XXFC_API int64_t xx_tar_gz_get_uncompressed_size(const xx_tar_gz *tar_gz);

static inline Abstractformat *xx_tar_gz_to_format(xx_tar_gz *tar_gz) {
    return tar_gz ? &tar_gz->format : NULL;
}

static inline const Abstractformat *xx_tar_gz_to_format_const(
    const xx_tar_gz *tar_gz) {
    return tar_gz ? &tar_gz->format : NULL;
}

static inline void XTarGz_init(xx_tar_gz *tar_gz, xx_io_device *dev,
                               int64_t base_address) {
    xx_tar_gz_init(tar_gz, dev, base_address);
}

static inline xx_tar_gz *XTarGz_create(xx_io_device *dev,
                                       int64_t base_address) {
    return xx_tar_gz_create(dev, base_address);
}

static inline void XTarGz_free(xx_tar_gz *tar_gz) {
    xx_tar_gz_free(tar_gz);
}

static inline xx_archive_write_state *XTarGz_create_archive_records_writing(
    xx_tar_gz *tar_gz, const xx_list_s *options, xx_pd_struct *pd) {
    return tar_gz ? xx_tar_gz_create_archive_records_writing(
                        &tar_gz->format, options, pd)
                  : NULL;
}

static inline bool XTarGz_pack_archive_record(
    xx_tar_gz *tar_gz, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd) {
    return tar_gz ? xx_tar_gz_pack_archive_record(
                        &tar_gz->format, state, record, source_dev, pd)
                  : false;
}

static inline bool XTarGz_finalize_archive_records_writing(
    xx_tar_gz *tar_gz, xx_archive_write_state *state, xx_pd_struct *pd) {
    return tar_gz ? xx_tar_gz_finalize_archive_records_writing(
                        &tar_gz->format, state, pd)
                  : false;
}

static inline void XTarGz_free_archive_records_writing(
    xx_tar_gz *tar_gz, xx_archive_write_state *state) {
    if (tar_gz)
        xx_tar_gz_free_archive_records_writing(&tar_gz->format, state);
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TAR_GZ_H */
