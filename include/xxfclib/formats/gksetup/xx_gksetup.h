/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_gksetup.h @brief GkSetup installer data file (SETUP.DAT) reader. */

#ifndef XXFCLIB_FORMAT_GKSETUP_H
#define XXFCLIB_FORMAT_GKSETUP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* GkSetup's SETUP.DAT / SETUP.DA_ payload container.  Every member is stored
 * verbatim, so the only codec involved is a byte copy. */
typedef struct xx_gksetup {
    Abstractformat format;
    uint64_t number_of_records; /**< Files only; directory records are paths. */
    int64_t data_offset;        /**< Offset of the first chain record. */
    int64_t archive_end;        /**< End of the last member's payload. */
    bool has_padding;           /**< A zero uint32 sits before every payload. */
} xx_gksetup;

typedef xx_gksetup xx_gksetup_t;
typedef xx_gksetup XGkSetup;

XXFC_API void xx_gksetup_init(xx_gksetup *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_gksetup *xx_gksetup_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_gksetup_destroy(xx_gksetup *archive);
XXFC_API void xx_gksetup_free(xx_gksetup *archive);

XXFC_API bool xx_gksetup_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_gksetup_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_gksetup_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_gksetup_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gksetup_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gksetup_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gksetup_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gksetup_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gksetup_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_gksetup_to_format(xx_gksetup *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GKSETUP_H */
