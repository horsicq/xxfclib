/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_sqz.h @brief Squeeze It (HLSQZ) archive reader. */

#ifndef XXFCLIB_FORMAT_SQZ_H
#define XXFCLIB_FORMAT_SQZ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Registration pending.
 *
 * xxfc_defs.h carries no XX_FILE_TYPE_SQZ enumerator yet, and that header is
 * shared, so it is not edited from here. The provisional number below keeps
 * this reader self-contained and compilable; the integrator adding the real
 * enumerator should redefine this to it (or delete the block once the
 * enumerator exists and substitute the enumerator at the single use site in
 * xx_sqz_init()). The value is NOT part of the on-disk format. */
#ifndef XX_SQZ_FILE_TYPE_ID
#define XX_SQZ_FILE_TYPE_ID XX_FILE_TYPE_SQZ
#endif

/**
 * @brief A Squeeze It archive: an eight-byte "HLSQZ" banner, then a chain of
 *        length-prefixed records -- members and opaque archive blocks mixed
 *        together -- closed by a zero length byte.
 */
typedef struct xx_sqz {
    Abstractformat format;
    uint64_t number_of_records;
    /** The banner's version byte, 0x20..0x7e; 0 when nothing is parsed yet. */
    uint32_t version;
} xx_sqz;

typedef xx_sqz xx_sqz_t;

XXFC_API void xx_sqz_init(xx_sqz *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_sqz *xx_sqz_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_sqz_destroy(xx_sqz *archive);
XXFC_API void xx_sqz_free(xx_sqz *archive);

XXFC_API bool xx_sqz_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sqz_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sqz_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_sqz_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sqz_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sqz_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sqz_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sqz_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sqz_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SQZ_H */
