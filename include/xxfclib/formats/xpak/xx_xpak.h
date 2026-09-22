/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_xpak.h @brief XPAK single-member container reader. */

#ifndef XXFCLIB_FORMAT_XPAK_H
#define XXFCLIB_FORMAT_XPAK_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An XPAK container: one 25-byte header and one packed stream.
 *
 *   0x00  char[4] "XPAK"
 *   0x04  u32 LE  archive size, counting this header
 *   0x08  char[13] member name, NUL padded to the full field (8.3 plus the
 *                  terminator); bytes after the first NUL are all zero
 *   0x15  u32 LE  unpacked size of the member
 *   0x19  the packed stream, running to the end of the archive
 *
 * Derived from the 65-file reference corpus: the field table above accounts
 * for every header byte of all 65, the archive-size word equals the real file
 * length in 64 of them (the 65th is truncated), and the name field is
 * printable and zero padded in every one.
 *
 * THE CODEC IS NOT IDENTIFIED.  Every stream in the corpus opens on the same
 * ten bytes - 09 ff fe 00 80 00 04 00 20 ff - and the byte entropy of the
 * payload runs 7.94 to 7.99 bits, which is an entropy coder rather than a
 * plain LZ.  Neither XArchive nor U3 carries a handler for it.  Members are
 * therefore listed with their real name and both real sizes, and unpack fails
 * closed rather than emitting plausible-looking garbage.
 */
typedef struct xx_xpak {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size;
    int64_t declared_size; /**< The u32 at 0x04, before truncation clamping. */
    bool truncated;
} xx_xpak;

typedef xx_xpak xx_xpak_t;

/** Length of the fixed, NUL-padded member name field at 0x08. */
#define XX_XPAK_NAME_FIELD 13

XXFC_API void xx_xpak_init(xx_xpak *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_xpak *xx_xpak_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_xpak_destroy(xx_xpak *archive);
XXFC_API void xx_xpak_free(xx_xpak *archive);

XXFC_API bool xx_xpak_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_xpak_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_xpak_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_xpak_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_xpak_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_xpak_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_xpak_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_xpak_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_xpak_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_XPAK_H */
