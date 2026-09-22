/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ha.h @brief HA archive reader. */

#ifndef XXFCLIB_FORMAT_HA_H
#define XXFCLIB_FORMAT_HA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An HA archive: a four-byte header holding the member count, followed by that many self-sized member headers, each immediately followed by its payload.
 */
typedef struct xx_ha {
    Abstractformat format;
    uint64_t number_of_records;
} xx_ha;

typedef xx_ha xx_ha_t;

XXFC_API void xx_ha_init(xx_ha *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_ha *xx_ha_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_ha_destroy(xx_ha *archive);
XXFC_API void xx_ha_free(xx_ha *archive);

XXFC_API bool xx_ha_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_ha_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_ha_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_ha_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ha_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ha_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ha_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ha_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ha_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_HA_H */
