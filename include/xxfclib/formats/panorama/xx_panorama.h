/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_panorama.h @brief Panorama (enciphered RAR) archive reader. */

#ifndef XXFCLIB_FORMAT_PANORAMA_H
#define XXFCLIB_FORMAT_PANORAMA_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_panorama xx_panorama;
typedef struct xx_panorama xx_panorama_t;
typedef struct xx_panorama XPanoramaArchive;

/**
 * @brief A Panorama archive: an ordinary RAR 4.x file under a whole-file,
 * position-keyed XOR whose 1024-byte pad is generated from one 32-bit seed.
 *
 * There is no container of its own - no header, no directory - so the reader
 * publishes exactly ONE record covering the whole file, the way the reference
 * (XArchive/archives/xpanoramaarchive.cpp) does.  Deciphering is the whole job;
 * the plaintext RAR is handed on to the RAR reader by whoever consumes the
 * extracted file.  No RAR logic is duplicated here.
 */
struct xx_panorama {
    Abstractformat format;
    uint64_t number_of_records; /**< Always 1 for a valid archive. */
    uint32_t seed;              /**< LCG seed recovered from the first 8 bytes. */
    int64_t archive_size;       /**< Whole input; the cipher is length preserving. */
};

XXFC_API void xx_panorama_init(xx_panorama *archive, xx_io_device *device,
                               int64_t base_address);
XXFC_API xx_panorama *xx_panorama_create(xx_io_device *device,
                                         int64_t base_address);
XXFC_API void xx_panorama_destroy(xx_panorama *archive);
XXFC_API void xx_panorama_free(xx_panorama *archive);

XXFC_API bool xx_panorama_check_is_valid(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API bool xx_panorama_handle_base_info(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API int64_t xx_panorama_get_format_size(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API uint64_t xx_panorama_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_panorama_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_panorama_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_panorama_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_panorama_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_panorama_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint32_t xx_panorama_get_seed(const xx_panorama *archive);
XXFC_API int64_t xx_panorama_get_archive_size(const xx_panorama *archive);

static inline Abstractformat *xx_panorama_to_format(xx_panorama *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PANORAMA_H */
