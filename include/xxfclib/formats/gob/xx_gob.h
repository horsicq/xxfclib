/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_gob.h @brief LucasArts GOB game archive reader. */

#ifndef XXFCLIB_FORMAT_GOB_H
#define XXFCLIB_FORMAT_GOB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A LucasArts GOB archive: a four-byte signature, a pointer to the
 * member table, and a table of (offset, size, name) triples. Both the Dark
 * Forces flavour ("GOB\\n", 13-byte names) and the Jedi Knight flavour
 * ("GOB ", 128-byte names) are read. Members are always stored.
 */
typedef struct xx_gob {
    Abstractformat format;
    uint64_t number_of_records;
} xx_gob;

typedef xx_gob xx_gob_t;

XXFC_API void xx_gob_init(xx_gob *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_gob *xx_gob_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_gob_destroy(xx_gob *archive);
XXFC_API void xx_gob_free(xx_gob *archive);

XXFC_API bool xx_gob_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_gob_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_gob_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_gob_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gob_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gob_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gob_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gob_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gob_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GOB_H */
