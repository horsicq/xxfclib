/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_tar_nextstep.h @brief Native NextStep TAR archive reader. */

#ifndef XXFCLIB_FORMAT_TAR_NEXTSTEP_H
#define XXFCLIB_FORMAT_TAR_NEXTSTEP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The historical NextStep TAR layout keeps 512-byte blocks and classic TAR
 * checksums, but expands the name field to 225 bytes and shifts all later
 * fields.  It is intentionally a separate reader from POSIX TAR.
 */
typedef struct xx_tar_nextstep {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    void *internal;
} xx_tar_nextstep;

typedef xx_tar_nextstep xx_tar_nextstep_t;
typedef xx_tar_nextstep XTarNextStep;

XXFC_API void xx_tar_nextstep_init(xx_tar_nextstep *archive,
                                   xx_io_device *device,
                                   int64_t base_address);
XXFC_API xx_tar_nextstep *xx_tar_nextstep_create(xx_io_device *device,
                                                  int64_t base_address);
XXFC_API void xx_tar_nextstep_destroy(xx_tar_nextstep *archive);
XXFC_API void xx_tar_nextstep_free(xx_tar_nextstep *archive);

XXFC_API bool xx_tar_nextstep_check_is_valid(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API bool xx_tar_nextstep_handle_base_info(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API int64_t xx_tar_nextstep_get_format_size(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API uint64_t xx_tar_nextstep_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_tar_nextstep_create_archive_records_reading(Abstractformat *self,
                                               const xx_list_s *options,
                                               xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_tar_nextstep_get_current_archive_record(Abstractformat *self,
                                            xx_archive_record_state *state);
XXFC_API bool xx_tar_nextstep_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tar_nextstep_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tar_nextstep_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_tar_nextstep_get_number_of_records(
    const xx_tar_nextstep *archive);
XXFC_API int64_t xx_tar_nextstep_get_archive_end(const xx_tar_nextstep *archive);

static inline Abstractformat *xx_tar_nextstep_to_format(
    xx_tar_nextstep *archive) {
    return archive ? &archive->format : NULL;
}

static inline void XTarNextStep_init(xx_tar_nextstep *archive,
                                     xx_io_device *device,
                                     int64_t base_address) {
    xx_tar_nextstep_init(archive, device, base_address);
}
static inline xx_tar_nextstep *XTarNextStep_create(xx_io_device *device,
                                                    int64_t base_address) {
    return xx_tar_nextstep_create(device, base_address);
}
static inline void XTarNextStep_free(xx_tar_nextstep *archive) {
    xx_tar_nextstep_free(archive);
}
static inline bool XTarNextStep_is_valid(xx_tar_nextstep *archive,
                                         xx_pd_struct *pd) {
    return archive ? xx_format_is_valid(&archive->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TAR_NEXTSTEP_H */
