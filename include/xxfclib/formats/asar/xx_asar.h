/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_asar.h @brief ASAR (Electron) archive reader. */

#ifndef XXFCLIB_FORMAT_ASAR_H
#define XXFCLIB_FORMAT_ASAR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An ASAR archive, as produced by Electron.
 *
 * The container is a Chromium Pickle holding one JSON string, then a blob of
 * concatenated file contents. The JSON is a directory tree: an entry with a
 * "files" member is a folder, one with "link" is a symlink, and anything else
 * is a file whose "offset" (a decimal string, not a number) and "size" locate
 * it inside the blob. Nothing is compressed.
 *
 * Entries marked "unpacked" are the exception: their bytes are NOT in the
 * archive at all but in a sibling "<name>.unpacked" directory. They are
 * enumerated, because leaving them out would misreport the tree, but they
 * cannot be extracted from a device that has no path of its own.
 *
 * Symlink entries are enumerated too, with their target on the record as
 * XX_META_ID_LINK_TARGET, but extracting one fails: a link is not a byte
 * stream, and creating it is a decision about privilege and policy that
 * belongs to the caller.
 */
typedef struct xx_asar {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t json_offset;  /**< Always 16. */
    int64_t json_size;    /**< Bytes of JSON directory. */
    int64_t blob_offset;  /**< Where file contents begin. */
} xx_asar;

typedef xx_asar xx_asar_t;
typedef xx_asar XAsar;

XXFC_API void xx_asar_init(xx_asar *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_asar *xx_asar_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_asar_destroy(xx_asar *archive);
XXFC_API void xx_asar_free(xx_asar *archive);

XXFC_API bool xx_asar_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_asar_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_asar_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_asar_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_asar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_asar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_asar_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_asar_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_asar_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** @brief Offset at which file contents begin. */
XXFC_API int64_t xx_asar_get_blob_offset(const xx_asar *archive);
/** @brief Size of the JSON directory, in bytes. */
XXFC_API int64_t xx_asar_get_json_size(const xx_asar *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ASAR_H */
