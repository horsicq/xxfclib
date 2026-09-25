/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_installshield_skin.h @brief InstallShield skin (setup.skin) reader. */

#ifndef XXFCLIB_FORMAT_INSTALLSHIELD_SKIN_H
#define XXFCLIB_FORMAT_INSTALLSHIELD_SKIN_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An InstallShield skin file ("setup.skin", the dialog skin an
 * InstallScript setup.exe loads next to itself).
 *
 * The whole file is obfuscated byte by byte.  A byte at offset @c i from the
 * start of the skin decodes as
 *
 *     plain = ((raw >> 4) | (raw << 4)) ^ key[i & 7]
 *     key   = A2 85 59 BC A3 9F 3B AC
 *
 * (a nibble swap, then an 8-byte repeating XOR key).  The decoded content is
 * a flat run of members with no header, directory or terminator:
 *
 *     char name[]   1..260 bytes 0x20..0x7F, NUL terminated
 *     char size[]   1..19 decimal digits, NUL terminated
 *     u8   data[size]
 *
 * repeated until the end of the file.  Names are plain file names such as
 * "skin.ini" or "ButtonNormal.gif"; a separator in a name is honoured as a
 * sub-directory on extraction.
 */
typedef struct xx_installshield_skin {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< Sum of all member sizes. */
} xx_installshield_skin;

typedef xx_installshield_skin xx_installshield_skin_t;

/** Longest member name accepted, excluding the terminator. */
#define XX_INSTALLSHIELD_SKIN_NAME_MAX 260
/** Most decimal digits accepted in a size field. */
#define XX_INSTALLSHIELD_SKIN_DIGITS_MAX 19
/** Most members accepted in one skin. */
#define XX_INSTALLSHIELD_SKIN_MAX_RECORDS 4096

XXFC_API void xx_installshield_skin_init(xx_installshield_skin *archive,
                                         xx_io_device *device,
                                         int64_t base_address);
XXFC_API xx_installshield_skin *xx_installshield_skin_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_installshield_skin_destroy(xx_installshield_skin *archive);
XXFC_API void xx_installshield_skin_free(xx_installshield_skin *archive);

XXFC_API bool xx_installshield_skin_check_is_valid(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API bool xx_installshield_skin_handle_base_info(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API int64_t xx_installshield_skin_get_format_size(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_installshield_skin_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_installshield_skin_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_installshield_skin_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_installshield_skin_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_installshield_skin_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_installshield_skin_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/**
 * @brief Encode or decode @p size bytes in place.
 *
 * @param position offset of @p data[0] from the start of the skin; it picks
 *                 the key phase.
 * @param encode   false turns file bytes into plain bytes, true the reverse.
 */
XXFC_API void xx_installshield_skin_transform(uint8_t *data, size_t size,
                                              uint64_t position, bool encode);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INSTALLSHIELD_SKIN_H */
