/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_vmdk_cowd_sparse.h @brief VMware COWD sparse extent (ESX/GSX vmfsSparse) disk image. */

#ifndef XXFCLIB_FORMAT_VMDK_COWD_SPARSE_H
#define XXFCLIB_FORMAT_VMDK_COWD_SPARSE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A VMware COWD sparse extent (ESX/GSX, vmfsSparse delta disk): a one-level
 * grain directory and 4096-entry grain tables map the virtual disk onto the
 * file, presented as one flat stream. */
typedef struct xx_vmdk_cowd_sparse {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_vmdk_cowd_sparse;

typedef struct xx_vmdk_cowd_sparse xx_vmdk_cowd_sparse_t;

XXFC_API void xx_vmdk_cowd_sparse_init(xx_vmdk_cowd_sparse *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_vmdk_cowd_sparse *xx_vmdk_cowd_sparse_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_vmdk_cowd_sparse_destroy(xx_vmdk_cowd_sparse *archive);
XXFC_API void xx_vmdk_cowd_sparse_free(xx_vmdk_cowd_sparse *archive);
XXFC_API bool xx_vmdk_cowd_sparse_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_vmdk_cowd_sparse_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_vmdk_cowd_sparse_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_vmdk_cowd_sparse_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_vmdk_cowd_sparse_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vmdk_cowd_sparse_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vmdk_cowd_sparse_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_vmdk_cowd_sparse_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vmdk_cowd_sparse_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_vmdk_cowd_sparse_to_format(xx_vmdk_cowd_sparse *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VMDK_COWD_SPARSE_H */
