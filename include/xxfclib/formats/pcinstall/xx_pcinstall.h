/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pcinstall.h @brief PC-Install installer data file (*.SHR). */

#ifndef XXFCLIB_FORMAT_PCINSTALL_H
#define XXFCLIB_FORMAT_PCINSTALL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PC-Install's compact data container: a 0x38-byte volume header carrying the
 * member count, then a flat chain of 0x30-byte descriptors each followed by
 * one raw PKWARE DCL "implode" stream. */
typedef struct xx_pcinstall {
    Abstractformat format;
    uint64_t number_of_records;
    uint16_t declared_count; /**< Member count from the volume header. */
} xx_pcinstall;

typedef xx_pcinstall xx_pcinstall_t;
typedef xx_pcinstall XPCInstall;

XXFC_API void xx_pcinstall_init(xx_pcinstall *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_pcinstall *xx_pcinstall_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_pcinstall_destroy(xx_pcinstall *archive);
XXFC_API void xx_pcinstall_free(xx_pcinstall *archive);

XXFC_API bool xx_pcinstall_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_pcinstall_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_pcinstall_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_pcinstall_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pcinstall_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pcinstall_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pcinstall_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pcinstall_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pcinstall_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_pcinstall_to_format(xx_pcinstall *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PCINSTALL_H */
