/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_valve_vpk.h @brief Valve VPK archive reader. */

#ifndef XXFCLIB_FORMAT_VALVE_VPK_H
#define XXFCLIB_FORMAT_VALVE_VPK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Valve VPK container. */
typedef struct xx_valve_vpk_volume {
    uint16_t index;
    xx_io_device *device; /**< Borrowed; caller retains ownership. */
} xx_valve_vpk_volume;

typedef struct xx_valve_vpk {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unavailable_members;
    uint64_t unsupported_members;
    xx_valve_vpk_volume *volumes;
    size_t volume_count;
} xx_valve_vpk;

typedef xx_valve_vpk xx_valve_vpk_t;

XXFC_API void xx_valve_vpk_init(xx_valve_vpk *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_valve_vpk *xx_valve_vpk_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_valve_vpk_destroy(xx_valve_vpk *archive);
XXFC_API void xx_valve_vpk_free(xx_valve_vpk *archive);

/** Supply a borrowed external archive_NNN.vpk volume. NULL removes it. */
XXFC_API bool xx_valve_vpk_set_volume(xx_valve_vpk *archive, uint16_t index, xx_io_device *device);

XXFC_API bool xx_valve_vpk_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_valve_vpk_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_valve_vpk_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_valve_vpk_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_valve_vpk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_valve_vpk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_valve_vpk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_valve_vpk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_valve_vpk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_valve_vpk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_valve_vpk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_valve_vpk_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_VALVE_VPK_H */
