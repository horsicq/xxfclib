/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_iso9660.h @brief ISO 9660 primary-volume archive reader. */

#ifndef XXFCLIB_FORMAT_ISO9660_H
#define XXFCLIB_FORMAT_ISO9660_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_iso9660 xx_iso9660;
typedef struct xx_iso9660 xx_iso9660_t;
typedef struct xx_iso9660 XIso9660;

struct xx_iso9660 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t number_of_members;
    uint32_t logical_block_size;
    uint32_t volume_space_size;
    int64_t volume_end;
    void *internal;
};

XXFC_API void xx_iso9660_init(xx_iso9660 *iso, xx_io_device *dev,
                               int64_t base_address);
XXFC_API xx_iso9660 *xx_iso9660_create(xx_io_device *dev,
                                        int64_t base_address);
XXFC_API void xx_iso9660_destroy(xx_iso9660 *iso);
XXFC_API void xx_iso9660_free(xx_iso9660 *iso);

XXFC_API bool xx_iso9660_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_iso9660_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_iso9660_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_iso9660_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_iso9660_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_iso9660_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_iso9660_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_iso9660_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_iso9660_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_iso9660_get_number_of_records(const xx_iso9660 *iso);
XXFC_API uint64_t xx_iso9660_get_number_of_members(const xx_iso9660 *iso);
XXFC_API uint32_t xx_iso9660_get_logical_block_size(const xx_iso9660 *iso);
XXFC_API uint32_t xx_iso9660_get_volume_space_size(const xx_iso9660 *iso);
XXFC_API int64_t xx_iso9660_get_volume_end(const xx_iso9660 *iso);

static inline Abstractformat *xx_iso9660_to_format(xx_iso9660 *iso) {
    return iso ? &iso->format : NULL;
}
static inline void XIso9660_init(xx_iso9660 *iso, xx_io_device *dev,
                                 int64_t base_address) {
    xx_iso9660_init(iso, dev, base_address);
}
static inline xx_iso9660 *XIso9660_create(xx_io_device *dev,
                                           int64_t base_address) {
    return xx_iso9660_create(dev, base_address);
}
static inline void XIso9660_free(xx_iso9660 *iso) { xx_iso9660_free(iso); }
static inline bool XIso9660_is_valid(xx_iso9660 *iso, xx_pd_struct *pd) {
    return iso ? xx_format_is_valid(&iso->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ISO9660_H */
