/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_vhdx.h @brief Microsoft VHDX virtual hard disk (fixed, dynamic,
 * differencing). */

#ifndef XXFCLIB_FORMAT_VHDX_H
#define XXFCLIB_FORMAT_VHDX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A VHDX image: the "vhdxfile" identifier at offset 0, two 4 KiB headers
 * (64 KiB, 128 KiB), two region tables (192 KiB, 256 KiB) that locate the
 * block allocation table and the metadata region, and 1 MiB aligned payload
 * and sector bitmap blocks.  A non-empty metadata log is replayed in memory;
 * the file itself is never written.  The guest disk is published as ONE
 * member, "disk.img", rebuilt through the BAT. */
typedef struct xx_vhdx {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;          /**< base_address + format size, or -1. */
    uint64_t disk_size;           /**< Guest disk size in bytes. */
    uint32_t disk_type;           /**< 2 fixed, 3 dynamic, 4 differencing. */
    uint32_t block_size;          /**< Payload block size (1..256 MiB). */
    uint32_t logical_sector_size; /**< 512 or 4096. */
    uint32_t physical_sector_size;/**< 512 or 4096 (0 when not recorded). */
    uint64_t blocks;              /**< Payload blocks covering the disk. */
    uint64_t blocks_present;      /**< Fully or partially present blocks. */
    uint32_t log_entries_replayed;/**< Log entries applied in memory. */
} xx_vhdx;

typedef struct xx_vhdx xx_vhdx_t;

XXFC_API void xx_vhdx_init(xx_vhdx *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_vhdx *xx_vhdx_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_vhdx_destroy(xx_vhdx *archive);
XXFC_API void xx_vhdx_free(xx_vhdx *archive);
XXFC_API bool xx_vhdx_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_vhdx_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_vhdx_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_vhdx_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_vhdx_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_vhdx_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_vhdx_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_vhdx_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_vhdx_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_vhdx_to_format(xx_vhdx *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VHDX_H */
