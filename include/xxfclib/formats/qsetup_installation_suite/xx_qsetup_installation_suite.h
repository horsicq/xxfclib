/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_qsetup_installation_suite.h
 *  @brief QSetup Installation Suite (Pantaray Research) self-extractor. */

#ifndef XXFCLIB_FORMAT_QSETUP_INSTALLATION_SUITE_H
#define XXFCLIB_FORMAT_QSETUP_INSTALLATION_SUITE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A QSetup installer: a PE32 stub whose overlay holds the container.
 *
 * The container starts exactly at the PE overlay:
 *
 *   u32 n1, n1 bytes  separator string, "|" or "||"
 *   u32 n2, n2 bytes  product list, opens with "|http:"
 *   records           u32 size, then size bytes of one complete zlib stream
 *   trailer           0x4A bytes: +0x04 u32 container offset, +0x08 u32
 *                     record count, +0x0C u32 4A3B2C1D, +0x46 u32 0x4A
 *
 * A record decodes to a NUL-terminated header line "|name|seconds|" (seconds
 * since 1980-01-01) followed by the file body.  A trailing '*' on the name
 * marks the file to run after the install, and payload files carry an index
 * key "NNNNN#" in front of the installed name.
 */
typedef struct xx_qsetup_installation_suite {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t container_offset; /**< Absolute device offset of the container. */
    int64_t records_offset;   /**< Absolute device offset of the first record. */
    bool complete;            /**< The trailer was reached and agreed. */
} xx_qsetup_installation_suite;

typedef xx_qsetup_installation_suite xx_qsetup_installation_suite_t;

XXFC_API void xx_qsetup_installation_suite_init(
    xx_qsetup_installation_suite *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_qsetup_installation_suite *xx_qsetup_installation_suite_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_qsetup_installation_suite_destroy(
    xx_qsetup_installation_suite *archive);
XXFC_API void xx_qsetup_installation_suite_free(
    xx_qsetup_installation_suite *archive);

XXFC_API bool xx_qsetup_installation_suite_check_is_valid(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API bool xx_qsetup_installation_suite_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_qsetup_installation_suite_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_qsetup_installation_suite_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_qsetup_installation_suite_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_qsetup_installation_suite_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_qsetup_installation_suite_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_qsetup_installation_suite_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_qsetup_installation_suite_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_qsetup_installation_suite_to_format(
    xx_qsetup_installation_suite *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QSETUP_INSTALLATION_SUITE_H */
