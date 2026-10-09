/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_solitaire_deluxe.h @brief Solitaire Deluxe installer volume.
 * Parses its fixed directory and category table, then exposes only members
 * whose complete PKWARE DCL streams fit this volume and validate exactly.
 * Continuation members require later volumes and are not fabricated.
 */
#ifndef XXFCLIB_FORMAT_SOLITAIRE_DELUXE_H
#define XXFCLIB_FORMAT_SOLITAIRE_DELUXE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_solitaire_deluxe {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t declared_records;
    bool needs_continuation;
} xx_solitaire_deluxe;
typedef xx_solitaire_deluxe xx_solitaire_deluxe_t;
XXFC_API void xx_solitaire_deluxe_init(xx_solitaire_deluxe *,
                                       xx_io_device *, int64_t);
XXFC_API xx_solitaire_deluxe *xx_solitaire_deluxe_create(
    xx_io_device *, int64_t);
XXFC_API void xx_solitaire_deluxe_destroy(xx_solitaire_deluxe *);
XXFC_API void xx_solitaire_deluxe_free(xx_solitaire_deluxe *);
XXFC_API bool xx_solitaire_deluxe_check_is_valid(Abstractformat *,
                                                 xx_pd_struct *);
XXFC_API bool xx_solitaire_deluxe_handle_base_info(Abstractformat *,
                                                   xx_pd_struct *);
XXFC_API int64_t xx_solitaire_deluxe_get_format_size(Abstractformat *,
                                                     xx_pd_struct *);
XXFC_API uint64_t xx_solitaire_deluxe_get_number_of_archive_records(
    Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *
xx_solitaire_deluxe_create_archive_records_reading(
    Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *
xx_solitaire_deluxe_get_current_archive_record(
    Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_solitaire_deluxe_archive_record_move_to_next(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_solitaire_deluxe_unpack_current_archive_record(
    Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_solitaire_deluxe_free_archive_records_reading(
    Abstractformat *, xx_archive_record_state *);
static inline Abstractformat *xx_solitaire_deluxe_to_format(
    xx_solitaire_deluxe *archive) {
    return archive ? &archive->format : NULL;
}
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_solitaire_deluxe_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_solitaire_deluxe_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_solitaire_deluxe_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_solitaire_deluxe_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_solitaire_deluxe_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
