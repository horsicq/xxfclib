/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_vmdk_sesparse.h @brief VMware seSparse (space-efficient sparse) VMDK extent. */

#ifndef XXFCLIB_FORMAT_VMDK_SESPARSE_H
#define XXFCLIB_FORMAT_VMDK_SESPARSE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A VMware seSparse extent (the *-sesparse.vmdk file of an ESXi 6.5+
 * snapshot or disk): a u64 grain directory and u64 grain tables map 4 KiB
 * grains onto the file.  Presented as one flat disk image. */
typedef struct xx_vmdk_sesparse {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
} xx_vmdk_sesparse;

typedef struct xx_vmdk_sesparse xx_vmdk_sesparse_t;

XXFC_API void xx_vmdk_sesparse_init(xx_vmdk_sesparse *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_vmdk_sesparse *xx_vmdk_sesparse_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_vmdk_sesparse_destroy(xx_vmdk_sesparse *archive);
XXFC_API void xx_vmdk_sesparse_free(xx_vmdk_sesparse *archive);
XXFC_API bool xx_vmdk_sesparse_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_vmdk_sesparse_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_vmdk_sesparse_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_vmdk_sesparse_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_vmdk_sesparse_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vmdk_sesparse_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vmdk_sesparse_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_vmdk_sesparse_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vmdk_sesparse_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_vmdk_sesparse_to_format(xx_vmdk_sesparse *archive)
{
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VMDK_SESPARSE_H */
