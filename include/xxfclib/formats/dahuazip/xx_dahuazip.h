/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_dahuazip.h @brief Dahua "DH\x03\x04" ZIP firmware reader. */

/* Dahua ships IP-camera and NVR firmware as an ordinary ZIP archive whose
 * very first local file header has its "PK" signature overwritten with "DH".
 * Nothing else is changed: the remaining local headers, the central directory
 * ("PK\x01\x02") and the end-of-central-directory record ("PK\x05\x06") are
 * stock ZIP, and every offset in them is relative to the "DH" byte.
 *
 *   +0x00  "DH\x03\x04"                  (stock ZIP: "PK\x03\x04")
 *   +0x04  the rest of the 30-byte local file header, unchanged
 *   ...    members, central directory, EOCD, optional archive comment
 *
 * Source: binwalk's src/signatures/dahua_zip.rs (magic "DH\x03\x04", then the
 * stock zip_parser), src/signatures/zip.rs + src/structures/zip.rs (local
 * header and EOCD validation, carve length) and src/extractors/dahua_zip.rs,
 * which rewrites exactly the two bytes at the start of the carve back to "PK"
 * and leaves everything else as it is.
 *
 * The reader does not re-implement ZIP.  It presents the library's own ZIP
 * reader (xx_zip) with a bounded, read-only view of the carve in which bytes
 * 0 and 1 read as "PK", and publishes that reader's members as its records.
 */

#ifndef XXFCLIB_FORMAT_DAHUAZIP_H
#define XXFCLIB_FORMAT_DAHUAZIP_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_DAHUAZIP_MAGIC "DH\x03\x04"
#define XX_DAHUAZIP_MAGIC_SIZE 4U
/** The two bytes binwalk's extractor rewrites ("DH" -> "PK"). */
#define XX_DAHUAZIP_PATCH_SIZE 2U
#define XX_DAHUAZIP_LOCAL_HEADER_SIZE 30U
#define XX_DAHUAZIP_EOCD_SIZE 22U

typedef struct xx_dahuazip xx_dahuazip;
typedef struct xx_dahuazip xx_dahuazip_t;
typedef struct xx_dahuazip XDahuazip;

struct xx_dahuazip {
    Abstractformat format;
    uint64_t number_of_records; /**< Central directory entries (xx_zip). */
    uint16_t version_needed;    /**< First local header, "version" word. */
    uint16_t flags;             /**< First local header, general flags. */
    uint16_t compression;       /**< First local header, method. */
    uint16_t comment_size;      /**< EOCD archive comment length. */
    bool is_zip64;              /**< ZIP64 end records present (xx_zip). */
    int64_t eocd_offset;        /**< Absolute offset of the EOCD, or -1. */
    int64_t archive_end;        /**< Absolute end of the carve, or -1. */
    void *internal;
};

XXFC_API void xx_dahuazip_init(xx_dahuazip *dahuazip, xx_io_device *dev,
                               int64_t base_address);
XXFC_API xx_dahuazip *xx_dahuazip_create(xx_io_device *dev,
                                         int64_t base_address);
XXFC_API void xx_dahuazip_destroy(xx_dahuazip *dahuazip);
XXFC_API void xx_dahuazip_free(xx_dahuazip *dahuazip);

XXFC_API bool xx_dahuazip_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_dahuazip_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_dahuazip_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_dahuazip_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_dahuazip_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_dahuazip_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_dahuazip_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_dahuazip_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_dahuazip_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_dahuazip_get_number_of_records(
    const xx_dahuazip *dahuazip);
XXFC_API int64_t xx_dahuazip_get_eocd_offset(const xx_dahuazip *dahuazip);
XXFC_API int64_t xx_dahuazip_get_archive_end(const xx_dahuazip *dahuazip);
XXFC_API bool xx_dahuazip_is_zip64(const xx_dahuazip *dahuazip);

static inline Abstractformat *xx_dahuazip_to_format(xx_dahuazip *dahuazip) {
    return dahuazip ? &dahuazip->format : NULL;
}
static inline void XDahuazip_init(xx_dahuazip *dahuazip, xx_io_device *dev,
                                  int64_t base_address) {
    xx_dahuazip_init(dahuazip, dev, base_address);
}
static inline xx_dahuazip *XDahuazip_create(xx_io_device *dev,
                                            int64_t base_address) {
    return xx_dahuazip_create(dev, base_address);
}
static inline void XDahuazip_free(xx_dahuazip *dahuazip) {
    xx_dahuazip_free(dahuazip);
}
static inline bool XDahuazip_is_valid(xx_dahuazip *dahuazip,
                                      xx_pd_struct *pd) {
    return dahuazip ? xx_format_is_valid(&dahuazip->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_DAHUAZIP_H */
