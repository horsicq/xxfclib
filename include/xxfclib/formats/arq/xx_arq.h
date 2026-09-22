/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_arq.h @brief ARQ / Crusher! archive reader. */

#ifndef XXFCLIB_FORMAT_ARQ_H
#define XXFCLIB_FORMAT_ARQ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An ARQ archive, as written by Crusher!.
 *
 * Members are chained: each carries its own header, name and payload, and the
 * chain ends with a header-shaped record whose name length is zero. A wrapping
 * twelve-byte prelude appears only when Crusher! encloses the chain, as it
 * does inside a self-extracting stub; a bare .ARQ, .SKU or .IRD file begins
 * directly at the first member.
 *
 * Two storage methods exist: stored, and "crushed", which is LHA's -lh5-.
 * Every member also carries a CRC-32 of its *packed* bytes, which this reader
 * verifies during parsing -- it is the strongest structural anchor the format
 * offers, and checking it is what makes the four-byte magic trustworthy.
 */
typedef struct xx_arq {
    Abstractformat format;
    uint64_t number_of_records;
    bool has_container_header;   /**< The twelve-byte wrapping prelude. */
    uint32_t declared_uncompressed_size; /**< Only meaningful when wrapped. */
} xx_arq;

typedef xx_arq xx_arq_t;
typedef xx_arq XArq;

XXFC_API void xx_arq_init(xx_arq *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_arq *xx_arq_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_arq_destroy(xx_arq *archive);
XXFC_API void xx_arq_free(xx_arq *archive);

XXFC_API bool xx_arq_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_arq_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_arq_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_arq_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_arq_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_arq_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_arq_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_arq_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_arq_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** @brief True when the twelve-byte wrapping prelude is present. */
XXFC_API bool xx_arq_has_container_header(const xx_arq *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ARQ_H */
