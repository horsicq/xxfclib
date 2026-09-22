/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ibmpack.h @brief IBM/OS-2 PACK ("A5 96") packed-file reader. */

#ifndef XXFCLIB_FORMAT_IBMPACK_H
#define XXFCLIB_FORMAT_IBMPACK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IBM PACK container.
 *
 * One file holds one or more members, each introduced by its own
 * 0xA5 0x96 header.  Three header dialects share the container and are told
 * apart by the little-endian word at offset 2: 0x0A14 (PACK 1), 0xFFFE
 * (PACK, chained, carries the plaintext length) and 0xFFFF (PACK, single
 * member, no length).  Every member's payload is the same 12-bit IBM LZW
 * stream.
 */
typedef struct xx_ibmpack {
    Abstractformat format;
    uint64_t number_of_records;
    /** Dialect word from offset 2 of the first member header. */
    uint32_t variant;
} xx_ibmpack;

typedef xx_ibmpack xx_ibmpack_t;

XXFC_API void xx_ibmpack_init(xx_ibmpack *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_ibmpack *xx_ibmpack_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_ibmpack_destroy(xx_ibmpack *archive);
XXFC_API void xx_ibmpack_free(xx_ibmpack *archive);

XXFC_API bool xx_ibmpack_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_ibmpack_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_ibmpack_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_ibmpack_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ibmpack_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ibmpack_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ibmpack_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ibmpack_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ibmpack_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IBMPACK_H */
