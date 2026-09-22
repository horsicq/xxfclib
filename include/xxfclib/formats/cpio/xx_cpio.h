/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_cpio.h @brief Native CPIO archive reader and newc writer. */

#ifndef XXFCLIB_FORMAT_CPIO_H
#define XXFCLIB_FORMAT_CPIO_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_cpio_variant_e {
    XX_CPIO_VARIANT_UNKNOWN = 0,
    XX_CPIO_VARIANT_NEWC,
    XX_CPIO_VARIANT_CRC,
    XX_CPIO_VARIANT_ODC,
    XX_CPIO_VARIANT_AFIO,
    XX_CPIO_VARIANT_BINARY_LE,
    XX_CPIO_VARIANT_BINARY_BE,
    /* Solaris block-compressed CPIO wrapper (0x199E 'TL' / 0x199E 'TG').
     * The members below the wrapper are ordinary newc/CRC records. */
    XX_CPIO_VARIANT_SOLARIS
} xx_cpio_variant_t;

typedef struct xx_cpio {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t archive_end;
    xx_cpio_variant_t variant;
} xx_cpio;

typedef xx_cpio xx_cpio_t;
typedef xx_cpio XCpio;

XXFC_API void xx_cpio_init(xx_cpio *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_cpio *xx_cpio_create(xx_io_device *device,
                                 int64_t base_address);
XXFC_API void xx_cpio_destroy(xx_cpio *archive);
XXFC_API void xx_cpio_free(xx_cpio *archive);

XXFC_API bool xx_cpio_check_is_valid(Abstractformat *self,
                                     xx_pd_struct *pd);
XXFC_API bool xx_cpio_handle_base_info(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API int64_t xx_cpio_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_cpio_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_cpio_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_cpio_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_cpio_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_cpio_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_cpio_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API xx_archive_write_state *xx_cpio_create_archive_records_writing(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API bool xx_cpio_pack_archive_record(
    Abstractformat *self, xx_archive_write_state *state,
    const xx_archive_record *record, xx_io_device *source_dev,
    xx_pd_struct *pd);
XXFC_API bool xx_cpio_finalize_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state, xx_pd_struct *pd);
XXFC_API void xx_cpio_free_archive_records_writing(
    Abstractformat *self, xx_archive_write_state *state);

static inline Abstractformat *xx_cpio_to_format(xx_cpio *archive) {
    return archive ? &archive->format : NULL;
}

static inline void XCpio_init(xx_cpio *archive, xx_io_device *device,
                               int64_t base_address) {
    xx_cpio_init(archive, device, base_address);
}
static inline xx_cpio *XCpio_create(xx_io_device *device,
                                    int64_t base_address) {
    return xx_cpio_create(device, base_address);
}
static inline void XCpio_free(xx_cpio *archive) {
    xx_cpio_free(archive);
}
static inline bool XCpio_is_valid(xx_cpio *archive, xx_pd_struct *pd) {
    return archive ? xx_format_is_valid(&archive->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CPIO_H */
