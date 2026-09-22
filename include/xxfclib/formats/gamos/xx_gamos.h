/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_gamos.h @brief GAMOS PACKED FILE (.gpf) archive reader. */

#ifndef XXFCLIB_FORMAT_GAMOS_H
#define XXFCLIB_FORMAT_GAMOS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_gamos xx_gamos;
typedef struct xx_gamos xx_gamos_t;
typedef struct xx_gamos XGamos;

/**
 * @brief A GAMOS PACKED FILE: a 33-byte header, a flat directory of 22-byte
 * records, and the payload immediately behind it.
 *
 * The resource container of the Russian studio Gamos (Snake, Wonderland).  A
 * game ships one .GPF plus companions in the same format carrying one video
 * mode's display data (.EGA / .VGA) or the music (.SND).  Both size fields are
 * 16 bit, so a member is capped at 64 KiB.
 */
struct xx_gamos {
    Abstractformat format;
    uint64_t number_of_records; /**< Members in the directory. */
    int64_t directory_offset;   /**< Always 0x21 relative to the base address. */
    int64_t directory_size;     /**< number_of_records * 22. */
    int64_t archive_size;       /**< End of the last member's payload. */
};

XXFC_API void xx_gamos_init(xx_gamos *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_gamos *xx_gamos_create(xx_io_device *device,
                                   int64_t base_address);
XXFC_API void xx_gamos_destroy(xx_gamos *archive);
XXFC_API void xx_gamos_free(xx_gamos *archive);

XXFC_API bool xx_gamos_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_gamos_handle_base_info(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API int64_t xx_gamos_get_format_size(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API uint64_t xx_gamos_get_number_of_archive_records(Abstractformat *self,
                                                         xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_gamos_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gamos_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gamos_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gamos_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gamos_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_gamos_get_number_of_records(const xx_gamos *archive);
XXFC_API int64_t xx_gamos_get_archive_size(const xx_gamos *archive);

static inline Abstractformat *xx_gamos_to_format(xx_gamos *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GAMOS_H */
