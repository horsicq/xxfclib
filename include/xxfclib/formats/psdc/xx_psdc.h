/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_psdc.h @brief PSDC packed-file reader (Print Shop Deluxe media). */

#ifndef XXFCLIB_FORMAT_PSDC_H
#define XXFCLIB_FORMAT_PSDC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A PSDC packed file.
 *
 * One container holds exactly one member.  The 80-byte header is a 76-byte
 * name buffer -- an 8.3 name, its NUL, then zeros -- followed by the
 * container's own total length, which equals the file size exactly.  The
 * payload starts at offset 0x50 and runs to the end of the file, and is a
 * PKWARE DCL ("implode") stream: the bytes at 0x50 and 0x51 are the codec's
 * own literal-mode and dictionary-size selectors, not a container tag.
 *
 * No plaintext length is stored, so the member's uncompressed size is
 * measured with xx_dcl_scan_memory() when the records are created.
 */
typedef struct xx_psdc {
    Abstractformat format;
    uint64_t number_of_records;
    /** The length the header declares for the whole container. */
    uint32_t declared_size;
} xx_psdc;

typedef xx_psdc xx_psdc_t;

XXFC_API void xx_psdc_init(xx_psdc *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_psdc *xx_psdc_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_psdc_destroy(xx_psdc *archive);
XXFC_API void xx_psdc_free(xx_psdc *archive);

XXFC_API bool xx_psdc_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_psdc_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_psdc_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_psdc_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_psdc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_psdc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_psdc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_psdc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_psdc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PSDC_H */
