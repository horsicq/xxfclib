/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_vmssaveset.h @brief OpenVMS BACKUP save set. */

#ifndef XXFCLIB_FORMAT_VMSSAVESET_H
#define XXFCLIB_FORMAT_VMSSAVESET_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An OpenVMS BACKUP save set: fixed-size blocks, each holding a packed
 * sequence of records.  A member's bytes are its body records joined. */
typedef struct xx_vmssaveset {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_vmssaveset;

typedef struct xx_vmssaveset xx_vmssaveset_t;

XXFC_API void xx_vmssaveset_init(xx_vmssaveset *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_vmssaveset *xx_vmssaveset_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_vmssaveset_destroy(xx_vmssaveset *archive);
XXFC_API void xx_vmssaveset_free(xx_vmssaveset *archive);
XXFC_API bool xx_vmssaveset_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_vmssaveset_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_vmssaveset_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_vmssaveset_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_vmssaveset_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vmssaveset_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vmssaveset_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_vmssaveset_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vmssaveset_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_vmssaveset_to_format(xx_vmssaveset *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VMSSAVESET_H */
