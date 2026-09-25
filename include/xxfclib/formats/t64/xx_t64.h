/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_t64.h @brief Commodore 64 T64 tape container reader. */

#ifndef XXFCLIB_FORMAT_T64_H
#define XXFCLIB_FORMAT_T64_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A T64 tape container (C64S emulator format, also written by VICE
 *        and many converters).
 *
 *   header, 64 bytes
 *     0x00  char[32] description: "C64 tape image file", "C64S tape file"
 *                    or "C64S tape image file", NUL padded
 *     0x20  u16 LE   version (0x0100 / 0x0101; not checked, as in VICE)
 *     0x22  u16 LE   directory slots
 *     0x24  u16 LE   used slots (often 0 or wrong; informational only)
 *     0x26  u16      reserved
 *     0x28  char[24] tape name, space padded
 *   directory slot, 32 bytes each, from 0x40
 *     0x00  u8       C64S entry type: 0 free, 1 normal file, 2 file with
 *                    header, 3 memory snapshot, 4 tape block, 5 stream
 *     0x01  u8       1541 file type (0x82 PRG, 0x81 SEQ, ...; often 0x01)
 *     0x02  u16 LE   start (load) address
 *     0x04  u16 LE   end address, one past the last byte (0 means 0x10000)
 *     0x06  u16      reserved
 *     0x08  u32 LE   offset of the data from the container start
 *     0x0C  u32      reserved
 *     0x10  char[16] PETSCII name, padded with 0x20 or 0xA0
 *
 * A member is extracted as a PRG file: the 2-byte load address followed by
 * the data, as Deark and VICE's c1541 write it.  The data length is
 * end - start; when that is impossible or runs into the next member (or
 * past the end of the container) - the well-known wrong-end-address T64s -
 * the length is the gap to the next member instead, as VICE does.
 */
typedef struct xx_t64 {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t version;
    uint32_t directory_slots;   /**< The u16 at 0x22. */
    uint32_t scanned_slots;     /**< Slots actually read as directory. */
    uint32_t used_slots;        /**< The u16 at 0x24, as stored. */
    uint32_t truncated_entries; /**< Entries whose data starts past EOF. */
    uint32_t skipped_entries;   /**< Reserved type or data inside the
                                     header / directory: not listed. */
    uint32_t fixed_entries;     /**< Entries whose end address was wrong. */
    char tape_name[25];
} xx_t64;

typedef xx_t64 xx_t64_t;

#define XX_T64_HEADER_SIZE 64
#define XX_T64_ENTRY_SIZE 32

XXFC_API void xx_t64_init(xx_t64 *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_t64 *xx_t64_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_t64_destroy(xx_t64 *archive);
XXFC_API void xx_t64_free(xx_t64 *archive);

XXFC_API bool xx_t64_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_t64_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_t64_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_t64_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_t64_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_t64_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_t64_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_t64_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_t64_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_T64_H */
