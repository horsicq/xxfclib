/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_tps.h @brief TPS (Clarion/TopSpeed) archive reader. */

#ifndef XXFCLIB_FORMAT_TPS_H
#define XXFCLIB_FORMAT_TPS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registration pending.
 *
 * xxfc_defs.h carries no XX_FILE_TYPE_TPS enumerator yet, and that header is
 * shared, so it is not edited from here. The provisional number below keeps
 * this reader self-contained and compilable; the integrator adding the real
 * enumerator should redefine this to it. The value is NOT part of the on-disk
 * format. */
#ifndef XX_TPS_FILE_TYPE_ID
#define XX_TPS_FILE_TYPE_ID XX_FILE_TYPE_TPS
#endif

/**
 * @brief A TPS archive: a four-byte "TPS\\x1a" banner, then a chain of
 *        fixed-width 0x16-byte directory entries each followed inline by its
 *        member payload, optionally closed by a single 0x04 byte.
 */
typedef struct xx_tps {
    Abstractformat format;
    uint64_t number_of_records;
} xx_tps;

typedef xx_tps xx_tps_t;

XXFC_API void xx_tps_init(xx_tps *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_tps *xx_tps_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_tps_destroy(xx_tps *archive);
XXFC_API void xx_tps_free(xx_tps *archive);

XXFC_API bool xx_tps_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_tps_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_tps_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_tps_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_tps_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_tps_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_tps_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_tps_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_tps_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_TPS_H */
