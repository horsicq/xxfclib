/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_pc98_d88.h @brief Native PC-98 D88 floppy-image reader.
 * Exact stored sectors and each D88 disk container are listable/extractable.
 * A reconstructed raw disk is additionally emitted for complete, healthy,
 * uniform CHS tracks only. Accepts 672/688-byte headers, end sentinels, and
 * up to eight concatenated disks within 32 MiB. Parsing allocates a bounded
 * temporary source buffer outside per-operation MEMORY_LIMIT; extraction
 * accounts for retained view and transfer memory.
 */
#ifndef XX_PC98_D88_H
#define XX_PC98_D88_H
#include "xxfclib/formats/xx_format.h"
typedef struct xx_pc98_d88 {Abstractformat format;} xx_pc98_d88;
XXFC_API void xx_pc98_d88_init(xx_pc98_d88 *,xx_io_device *,int64_t);
XXFC_API xx_pc98_d88 *xx_pc98_d88_create(xx_io_device *,int64_t);
XXFC_API void xx_pc98_d88_destroy(xx_pc98_d88 *);
XXFC_API void xx_pc98_d88_free(xx_pc98_d88 *);
XXFC_API bool xx_pc98_d88_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pc98_d88_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API int64_t xx_pc98_d88_get_format_size(Abstractformat *,xx_pd_struct *);
XXFC_API uint64_t xx_pc98_d88_get_number_of_archive_records(Abstractformat *,xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_pc98_d88_create_archive_records_reading(
    Abstractformat *,const xx_list_s *,xx_pd_struct *);
XXFC_API const xx_archive_record *xx_pc98_d88_get_current_archive_record(
    Abstractformat *,xx_archive_record_state *);
XXFC_API bool xx_pc98_d88_archive_record_move_to_next(
    Abstractformat *,xx_archive_record_state *,xx_pd_struct *);
XXFC_API bool xx_pc98_d88_unpack_current_archive_record(
    Abstractformat *,xx_archive_record_state *,xx_pd_struct *);
XXFC_API void xx_pc98_d88_free_archive_records_reading(
    Abstractformat *,xx_archive_record_state *);
XXFC_API bool xx_pc98_d88_extract_record_to_device(Abstractformat *,
    xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
#endif
