/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_valve_xzp.h @brief Valve XZP (Xbox Half-Life 2 pack) reader. */

#ifndef XXFCLIB_FORMAT_VALVE_XZP_H
#define XXFCLIB_FORMAT_VALVE_XZP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Valve XZP pack, the Xbox build of Half-Life 2's game archives.
 *
 * All fields are little-endian u32 unless stated otherwise.
 *
 *   0x00  char[4] "piZx"
 *   0x04  version, always 6
 *   0x08  preload directory entry count (P)
 *   0x0C  directory entry count (N)
 *   0x10  preload byte count
 *   0x14  header length, always 36
 *   0x18  directory item count (M)
 *   0x1C  directory item table offset (absolute)
 *   0x20  directory item table length in bytes (items plus their names)
 *   0x24  N directory entries: { name CRC, data length, data offset }
 *         then, only when the preload byte count is non-zero,
 *         P preload entries (same shape) and N u16 preload mappings
 *   ...   file data
 *   item  M directory items: { name CRC, absolute name offset, time },
 *         the NUL-terminated names follow inside the same table
 *   end   footer { u32 file length, char[4] "tFzX" }
 *
 * An entry is named by the first directory item with the same CRC. Stored
 * data is never compressed; the preload region only duplicates the first
 * bytes of some files and is not needed to extract them.
 */
typedef struct xx_valve_xzp {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unnamed_members; /**< Entries with no matching directory item. */
} xx_valve_xzp;

typedef xx_valve_xzp xx_valve_xzp_t;

XXFC_API void xx_valve_xzp_init(xx_valve_xzp *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_valve_xzp *xx_valve_xzp_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_valve_xzp_destroy(xx_valve_xzp *archive);
XXFC_API void xx_valve_xzp_free(xx_valve_xzp *archive);

XXFC_API bool xx_valve_xzp_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_valve_xzp_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_valve_xzp_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_valve_xzp_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_valve_xzp_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_valve_xzp_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_valve_xzp_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_valve_xzp_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_valve_xzp_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VALVE_XZP_H */
