/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_udf.h @brief UDF (ECMA-167 / OSTA UDF) filesystem archive reader. */

#ifndef XXFCLIB_FORMAT_UDF_H
#define XXFCLIB_FORMAT_UDF_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* xxfc_defs.h does not carry an XX_FILE_TYPE_UDF constant yet, and this port
 * is not allowed to edit the shared enum.  Once XX_FILE_TYPE_UDF is added,
 * replace the fallback below with
 *     #define XX_UDF_FILE_TYPE_ID XX_FILE_TYPE_UDF
 * and nothing else in this reader has to change.  215 is the first free value
 * after XX_FILE_TYPE_ZIE = 214 at the time of writing. */
#ifndef XX_UDF_FILE_TYPE_ID
#define XX_UDF_FILE_TYPE_ID XX_FILE_TYPE_UDF
#endif

typedef struct xx_udf xx_udf;
typedef struct xx_udf xx_udf_t;
typedef struct xx_udf XUdf;

struct xx_udf {
    Abstractformat format;
    uint64_t number_of_records;  /**< Files + directories listed. */
    uint64_t number_of_members;  /**< Files only (directories excluded). */
    uint32_t logical_block_size; /**< Logical block size from the LVD. */
    uint16_t udf_revision;       /**< BCD revision (0x0102 == UDF 1.02), 0 if unknown. */
    int64_t anchor_offset;       /**< File offset of the accepted AVDP, -1 if none. */
    int64_t volume_end;          /**< End of the described volume space, -1 if unknown. */
    char volume_identifier[256];
    char volume_set_identifier[512];
    void *internal;
};

XXFC_API void xx_udf_init(xx_udf *udf, xx_io_device *dev, int64_t base_address);
XXFC_API xx_udf *xx_udf_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_udf_destroy(xx_udf *udf);
XXFC_API void xx_udf_free(xx_udf *udf);

XXFC_API bool xx_udf_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_udf_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_udf_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_udf_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_udf_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_udf_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_udf_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_udf_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_udf_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_udf_get_number_of_records(const xx_udf *udf);
XXFC_API uint64_t xx_udf_get_number_of_members(const xx_udf *udf);
XXFC_API uint32_t xx_udf_get_logical_block_size(const xx_udf *udf);
XXFC_API uint16_t xx_udf_get_udf_revision(const xx_udf *udf);
XXFC_API int64_t xx_udf_get_anchor_offset(const xx_udf *udf);
XXFC_API int64_t xx_udf_get_volume_end(const xx_udf *udf);
XXFC_API const char *xx_udf_get_volume_identifier(const xx_udf *udf);
XXFC_API const char *xx_udf_get_volume_set_identifier(const xx_udf *udf);

/** Probe for the ECMA-167 2/9.1 Volume Recognition Sequence at offset 32768.
 * Cheap enough to be used as a detector prefilter; it does not parse the
 * volume, so a true result still has to be confirmed by
 * xx_udf_handle_base_info(). */
XXFC_API bool xx_udf_device_has_recognition_sequence(xx_io_device *dev,
                                                     int64_t base_address);

static inline Abstractformat *xx_udf_to_format(xx_udf *udf) {
    return udf ? &udf->format : NULL;
}
static inline void XUdf_init(xx_udf *udf, xx_io_device *dev,
                             int64_t base_address) {
    xx_udf_init(udf, dev, base_address);
}
static inline xx_udf *XUdf_create(xx_io_device *dev, int64_t base_address) {
    return xx_udf_create(dev, base_address);
}
static inline void XUdf_free(xx_udf *udf) { xx_udf_free(udf); }
static inline bool XUdf_is_valid(xx_udf *udf, xx_pd_struct *pd) {
    return udf ? xx_format_is_valid(&udf->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UDF_H */
