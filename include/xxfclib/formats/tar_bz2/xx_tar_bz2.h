/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_tar_bz2.h
 * @brief Bzip2-compressed TAR archive reader and writer.
 *
 * The bzip2 transport is decoded into a bounded logical TAR image. Archive
 * records and logical TAR data structures are then exposed through the normal
 * xxfclib archive interfaces.
 */

#ifndef XXFCLIB_FORMAT_TAR_BZ2_H
#define XXFCLIB_FORMAT_TAR_BZ2_H

#include "xxfclib/formats/tar/xx_tar.h"
#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tar_bz2 xx_tar_bz2;
typedef struct xx_tar_bz2 xx_tar_bz2_t;
typedef struct xx_tar_bz2 XTarBz2;

/** TAR structure identifiers, expressed in decoded (logical) TAR offsets. */
typedef xx_tar_data_struct_id_t xx_tar_bz2_data_struct_id_t;

#define XX_TAR_BZ2_DS_UNKNOWN     XX_TAR_DS_UNKNOWN
#define XX_TAR_BZ2_DS_HEADER      XX_TAR_DS_HEADER
#define XX_TAR_BZ2_DS_DATA        XX_TAR_DS_DATA
#define XX_TAR_BZ2_DS_PADDING     XX_TAR_DS_PADDING
#define XX_TAR_BZ2_DS_END_MARKERS XX_TAR_DS_END_MARKERS

struct xx_tar_bz2 {
    Abstractformat format;       /**< Base format; must be first. */
    uint64_t number_of_records;  /**< Visible TAR entries. */
    uint64_t number_of_members;  /**< Physical TAR members. */
    uint64_t uncompressed_size;  /**< Size of the decoded TAR image. */
    int64_t compressed_size;     /**< Exact bzip2 transport extent. */
    void *internal;              /**< Private shared compressed-TAR state. */
};

XXFC_API void xx_tar_bz2_init(xx_tar_bz2 *tar_bz2, xx_io_device *dev,
                              int64_t base_address);
XXFC_API xx_tar_bz2 *xx_tar_bz2_create(xx_io_device *dev,
                                       int64_t base_address);
XXFC_API void xx_tar_bz2_free(xx_tar_bz2 *tar_bz2);
XXFC_API void xx_tar_bz2_destroy(xx_tar_bz2 *tar_bz2);

XXFC_API bool xx_tar_bz2_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_tar_bz2_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_tar_bz2_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_tar_bz2_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tar_bz2_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tar_bz2_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tar_bz2_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tar_bz2_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_bz2_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API xx_archive_write_state *xx_tar_bz2_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_tar_bz2_pack_archive_record(
    Abstractformat *self, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd);
XXFC_API bool xx_tar_bz2_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_bz2_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state);

XXFC_API const char *xx_tar_bz2_data_struct_id_to_string(Abstractformat *self,
                                                         uint32_t id);
XXFC_API uint32_t xx_tar_bz2_data_struct_string_to_id(Abstractformat *self,
                                                      const char *name);
XXFC_API xx_data_struct_state *xx_tar_bz2_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_tar_bz2_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_tar_bz2_data_struct_move_to_next(
    Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_bz2_free_data_structs_reading(
    Abstractformat *self, xx_data_struct_state *state);

XXFC_API xx_data_struct_record_state *
xx_tar_bz2_create_data_struct_records_reading(Abstractformat *self,
                                               const xx_data_struct *ds,
                                               xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *
xx_tar_bz2_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_tar_bz2_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd);
XXFC_API void xx_tar_bz2_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint64_t xx_tar_bz2_get_number_of_records(
    const xx_tar_bz2 *tar_bz2);
XXFC_API uint64_t xx_tar_bz2_get_number_of_members(
    const xx_tar_bz2 *tar_bz2);
XXFC_API uint64_t xx_tar_bz2_get_uncompressed_size(
    const xx_tar_bz2 *tar_bz2);
XXFC_API int64_t xx_tar_bz2_get_compressed_size(
    const xx_tar_bz2 *tar_bz2);

static inline Abstractformat *xx_tar_bz2_to_format(xx_tar_bz2 *tar_bz2) {
    return tar_bz2 ? &tar_bz2->format : NULL;
}

static inline const Abstractformat *xx_tar_bz2_to_format_const(
    const xx_tar_bz2 *tar_bz2) {
    return tar_bz2 ? &tar_bz2->format : NULL;
}

static inline void XTarBz2_init(xx_tar_bz2 *tar_bz2, xx_io_device *dev,
                                int64_t base_address) {
    xx_tar_bz2_init(tar_bz2, dev, base_address);
}

static inline xx_tar_bz2 *XTarBz2_create(xx_io_device *dev,
                                         int64_t base_address) {
    return xx_tar_bz2_create(dev, base_address);
}

static inline void XTarBz2_free(xx_tar_bz2 *tar_bz2) {
    xx_tar_bz2_free(tar_bz2);
}

static inline bool XTarBz2_is_valid(xx_tar_bz2 *tar_bz2,
                                    xx_pd_struct *pd) {
    return tar_bz2 ? xx_format_is_valid(&tar_bz2->format, pd) : false;
}

static inline xx_archive_write_state *XTarBz2_create_archive_records_writing(
    xx_tar_bz2 *tar_bz2, const xx_list_s *options, xx_pd_struct *pd) {
    return tar_bz2 ? xx_tar_bz2_create_archive_records_writing(
                         &tar_bz2->format, options, pd)
                   : NULL;
}

static inline bool XTarBz2_pack_archive_record(
    xx_tar_bz2 *tar_bz2, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd) {
    return tar_bz2 ? xx_tar_bz2_pack_archive_record(
                         &tar_bz2->format, state, record, source_dev, pd)
                   : false;
}

static inline bool XTarBz2_finalize_archive_records_writing(
    xx_tar_bz2 *tar_bz2, xx_archive_write_state *state,
    xx_pd_struct *pd) {
    return tar_bz2 ? xx_tar_bz2_finalize_archive_records_writing(
                         &tar_bz2->format, state, pd)
                   : false;
}

static inline void XTarBz2_free_archive_records_writing(
    xx_tar_bz2 *tar_bz2, xx_archive_write_state *state) {
    if (tar_bz2)
        xx_tar_bz2_free_archive_records_writing(&tar_bz2->format, state);
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TAR_BZ2_H */
