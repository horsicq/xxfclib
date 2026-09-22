/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_shar.h @brief POSIX shell archive (shar) reader. */

#ifndef XXFCLIB_FORMAT_SHAR_H
#define XXFCLIB_FORMAT_SHAR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A shell archive: a /bin/sh script whose members are the bodies of
 * its `cat`/`sed` here-documents.  The reader only ever parses the text -
 * nothing in the script is executed - and members are stored, optionally
 * behind a one-character `sed 's/^X//'` quoting prefix.
 */
typedef struct xx_shar {
    Abstractformat format;
    uint64_t number_of_records;
} xx_shar;

typedef xx_shar xx_shar_t;

XXFC_API void xx_shar_init(xx_shar *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_shar *xx_shar_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_shar_destroy(xx_shar *archive);
XXFC_API void xx_shar_free(xx_shar *archive);

XXFC_API bool xx_shar_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_shar_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_shar_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_shar_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_shar_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_shar_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_shar_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_shar_archive_record_move_to_next(Abstractformat *self,
                                                  xx_archive_record_state *state,
                                                  xx_pd_struct *pd);
XXFC_API void xx_shar_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SHAR_H */
