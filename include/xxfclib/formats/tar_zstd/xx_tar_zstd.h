/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tar_zstd.h @brief Zstandard-compressed TAR archive reader/writer. */

#ifndef XXFCLIB_FORMAT_TAR_ZSTD_H
#define XXFCLIB_FORMAT_TAR_ZSTD_H

#include "xxfclib/formats/tar/xx_tar.h"
#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tar_zstd xx_tar_zstd;
typedef struct xx_tar_zstd xx_tar_zstd_t;
typedef struct xx_tar_zstd XTarZstd;

/** TAR structure identifiers, expressed in decoded TAR offsets. */
typedef xx_tar_data_struct_id_t xx_tar_zstd_data_struct_id_t;

#define XX_TAR_ZSTD_DS_UNKNOWN     XX_TAR_DS_UNKNOWN
#define XX_TAR_ZSTD_DS_HEADER      XX_TAR_DS_HEADER
#define XX_TAR_ZSTD_DS_DATA        XX_TAR_DS_DATA
#define XX_TAR_ZSTD_DS_PADDING     XX_TAR_DS_PADDING
#define XX_TAR_ZSTD_DS_END_MARKERS XX_TAR_DS_END_MARKERS

struct xx_tar_zstd {
    Abstractformat format;       /**< Base format; must be first. */
    uint64_t number_of_records;  /**< Visible TAR entries. */
    uint64_t number_of_members;  /**< Physical TAR members. */
    uint64_t uncompressed_size;  /**< Size of the decoded TAR image. */
    int64_t compressed_size;     /**< Exact Zstandard transport extent. */
    void *internal;              /**< Private shared compressed-TAR state. */
};

XXFC_API void xx_tar_zstd_init(xx_tar_zstd *tar_zstd, xx_io_device *dev,
                               int64_t base_address);
XXFC_API xx_tar_zstd *xx_tar_zstd_create(xx_io_device *dev,
                                         int64_t base_address);
XXFC_API void xx_tar_zstd_destroy(xx_tar_zstd *tar_zstd);
XXFC_API void xx_tar_zstd_free(xx_tar_zstd *tar_zstd);

XXFC_API bool xx_tar_zstd_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_tar_zstd_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_tar_zstd_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_tar_zstd_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tar_zstd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tar_zstd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tar_zstd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tar_zstd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_zstd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API xx_archive_write_state *xx_tar_zstd_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_tar_zstd_pack_archive_record(
    Abstractformat *self, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd);
XXFC_API bool xx_tar_zstd_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_zstd_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state);

XXFC_API const char *xx_tar_zstd_data_struct_id_to_string(
    Abstractformat *self, uint32_t id);
XXFC_API uint32_t xx_tar_zstd_data_struct_string_to_id(
    Abstractformat *self, const char *name);
XXFC_API xx_data_struct_state *xx_tar_zstd_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_tar_zstd_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_tar_zstd_data_struct_move_to_next(
    Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_zstd_free_data_structs_reading(
    Abstractformat *self, xx_data_struct_state *state);
XXFC_API xx_data_struct_record_state *
xx_tar_zstd_create_data_struct_records_reading(
    Abstractformat *self, const xx_data_struct *data_struct,
    xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *
xx_tar_zstd_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_tar_zstd_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd);
XXFC_API void xx_tar_zstd_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint64_t xx_tar_zstd_get_number_of_records(
    const xx_tar_zstd *tar_zstd);
XXFC_API uint64_t xx_tar_zstd_get_number_of_members(
    const xx_tar_zstd *tar_zstd);
XXFC_API uint64_t xx_tar_zstd_get_uncompressed_size(
    const xx_tar_zstd *tar_zstd);
XXFC_API int64_t xx_tar_zstd_get_compressed_size(
    const xx_tar_zstd *tar_zstd);

static inline Abstractformat *xx_tar_zstd_to_format(xx_tar_zstd *tar_zstd) {
    return tar_zstd ? &tar_zstd->format : NULL;
}

static inline void XTarZstd_init(xx_tar_zstd *tar_zstd, xx_io_device *dev,
                                 int64_t base_address) {
    xx_tar_zstd_init(tar_zstd, dev, base_address);
}
static inline xx_tar_zstd *XTarZstd_create(xx_io_device *dev,
                                            int64_t base_address) {
    return xx_tar_zstd_create(dev, base_address);
}
static inline void XTarZstd_free(xx_tar_zstd *tar_zstd) {
    xx_tar_zstd_free(tar_zstd);
}
static inline bool XTarZstd_is_valid(xx_tar_zstd *tar_zstd,
                                     xx_pd_struct *pd) {
    return tar_zstd ? xx_format_is_valid(&tar_zstd->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TAR_ZSTD_H */
