/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tar.h @brief Uncompressed TAR archive reader and writer. */

#ifndef XXFCLIB_FORMAT_TAR_H
#define XXFCLIB_FORMAT_TAR_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_tar xx_tar;
typedef struct xx_tar xx_tar_t;
typedef struct xx_tar XTar;

typedef enum xx_tar_data_struct_id_e {
    XX_TAR_DS_UNKNOWN = 0,
    XX_TAR_DS_HEADER,
    XX_TAR_DS_DATA,
    XX_TAR_DS_PADDING,
    XX_TAR_DS_END_MARKERS
} xx_tar_data_struct_id_t;

struct xx_tar {
    Abstractformat format;       /**< Base format; must be first. */
    uint64_t number_of_records;  /**< Visible archive entries. */
    uint64_t number_of_members;  /**< Physical headers, including metadata. */
    int64_t archive_end;         /**< Absolute end of TAR data. */
    void *internal;              /**< Private parsed representation. */
};

XXFC_API void xx_tar_init(xx_tar *tar, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_tar *xx_tar_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_tar_free(xx_tar *tar);
XXFC_API void xx_tar_destroy(xx_tar *tar);

XXFC_API bool xx_tar_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_tar_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_tar_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_tar_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tar_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tar_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Begin writing a POSIX USTAR archive to the format device.
 *
 * File and directory names are UTF-8. Paths longer than the 100-byte USTAR
 * name field are split at a directory separator into the 155-byte prefix and
 * 100-byte name fields when possible.
 */
XXFC_API xx_archive_write_state *xx_tar_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
/**
 * @brief Append one regular file or directory to a USTAR archive.
 *
 * Regular-file data is streamed from the beginning of @p source_dev. Passing
 * NULL creates an empty regular file. Directories are selected with
 * XX_META_ID_IS_FOLDER (or a trailing slash) and do not consume source data.
 */
XXFC_API bool xx_tar_pack_archive_record(
    Abstractformat *self, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd);
/** @brief Write the two zero TAR end records and finish the archive. */
XXFC_API bool xx_tar_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
/** @brief Release a TAR archive writing state. */
XXFC_API void xx_tar_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state);

XXFC_API const char *xx_tar_data_struct_id_to_string(Abstractformat *self,
                                                     uint32_t id);
XXFC_API uint32_t xx_tar_data_struct_string_to_id(Abstractformat *self,
                                                  const char *name);
XXFC_API xx_data_struct_state *xx_tar_create_data_structs_reading(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API const xx_data_struct *xx_tar_get_current_data_struct(
    Abstractformat *self, xx_data_struct_state *state);
XXFC_API bool xx_tar_data_struct_move_to_next(
    Abstractformat *self, xx_data_struct_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_free_data_structs_reading(
    Abstractformat *self, xx_data_struct_state *state);

XXFC_API xx_data_struct_record_state *
xx_tar_create_data_struct_records_reading(Abstractformat *self,
                                          const xx_data_struct *ds,
                                          xx_pd_struct *pd);
XXFC_API const xx_data_struct_record *xx_tar_get_current_data_struct_record(
    Abstractformat *self, xx_data_struct_record_state *state);
XXFC_API bool xx_tar_data_struct_record_move_to_next(
    Abstractformat *self, xx_data_struct_record_state *state,
    xx_pd_struct *pd);
XXFC_API void xx_tar_free_data_struct_records_reading(
    Abstractformat *self, xx_data_struct_record_state *state);

XXFC_API uint64_t xx_tar_get_number_of_records(const xx_tar *tar);
XXFC_API uint64_t xx_tar_get_number_of_members(const xx_tar *tar);
XXFC_API int64_t xx_tar_get_archive_end(const xx_tar *tar);

static inline Abstractformat *xx_tar_to_format(xx_tar *tar) {
    return tar ? &tar->format : NULL;
}

static inline const Abstractformat *xx_tar_to_format_const(const xx_tar *tar) {
    return tar ? &tar->format : NULL;
}

static inline void XTar_init(xx_tar *tar, xx_io_device *dev,
                             int64_t base_address) {
    xx_tar_init(tar, dev, base_address);
}

static inline xx_tar *XTar_create(xx_io_device *dev, int64_t base_address) {
    return xx_tar_create(dev, base_address);
}

static inline void XTar_free(xx_tar *tar) { xx_tar_free(tar); }

static inline xx_archive_write_state *XTar_create_archive_records_writing(
    xx_tar *tar, const xx_list_s *options, xx_pd_struct *pd) {
    return tar ? xx_tar_create_archive_records_writing(
                     &tar->format, options, pd)
               : NULL;
}

static inline bool XTar_pack_archive_record(
    xx_tar *tar, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd) {
    return tar ? xx_tar_pack_archive_record(&tar->format, state, record,
                                             source_dev, pd)
               : false;
}

static inline bool XTar_finalize_archive_records_writing(
    xx_tar *tar, xx_archive_write_state *state, xx_pd_struct *pd) {
    return tar ? xx_tar_finalize_archive_records_writing(
                     &tar->format, state, pd)
               : false;
}

static inline void XTar_free_archive_records_writing(
    xx_tar *tar, xx_archive_write_state *state) {
    if (tar) xx_tar_free_archive_records_writing(&tar->format, state);
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TAR_H */
