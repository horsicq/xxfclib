/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_bytekiller.h @brief Native ByteKiller-family single-file reader. */
#ifndef XXFCLIB_FORMAT_BYTEKILLER_H
#define XXFCLIB_FORMAT_BYTEKILLER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif

/** The eight supported wire layouts, all with exact native C decoding. */
typedef enum xx_bytekiller_variant {
    XX_BYTEKILLER_STANDARD,
    XX_BYTEKILLER_PRO,
    XX_BYTEKILLER_ACE,
    XX_BYTEKILLER_ANC,
    XX_BYTEKILLER_GRAC,
    XX_BYTEKILLER_MD10,
    XX_BYTEKILLER_MD11,
    XX_BYTEKILLER_JEK
} xx_bytekiller_variant;

typedef struct xx_bytekiller {
    Abstractformat format;
    uint64_t uncompressed_size;
    uint32_t variant;
} xx_bytekiller;

XXFC_API void xx_bytekiller_init(xx_bytekiller *archive,xx_io_device *device,int64_t base_address);
XXFC_API xx_bytekiller *xx_bytekiller_create(xx_io_device *device,int64_t base_address);
XXFC_API void xx_bytekiller_destroy(xx_bytekiller *archive);
XXFC_API void xx_bytekiller_free(xx_bytekiller *archive);
XXFC_API bool xx_bytekiller_check_is_valid(Abstractformat *self,xx_pd_struct *pd);
XXFC_API bool xx_bytekiller_handle_base_info(Abstractformat *self,xx_pd_struct *pd);
XXFC_API int64_t xx_bytekiller_get_format_size(Abstractformat *self,xx_pd_struct *pd);
XXFC_API uint64_t xx_bytekiller_get_number_of_archive_records(Abstractformat *self,xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_bytekiller_create_archive_records_reading(
    Abstractformat *self,const xx_list_s *options,xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_bytekiller_get_current_archive_record(
    Abstractformat *self,xx_archive_record_state *state);
XXFC_API bool xx_bytekiller_archive_record_move_to_next(
    Abstractformat *self,xx_archive_record_state *state,xx_pd_struct *pd);
XXFC_API bool xx_bytekiller_unpack_current_archive_record(
    Abstractformat *self,xx_archive_record_state *state,xx_pd_struct *pd);
XXFC_API void xx_bytekiller_free_archive_records_reading(
    Abstractformat *self,xx_archive_record_state *state);
XXFC_API bool xx_bytekiller_unpack_to_device(xx_bytekiller *archive,
    xx_io_device *destination,xx_pd_struct *pd);
#ifdef __cplusplus
}
#endif
#endif
