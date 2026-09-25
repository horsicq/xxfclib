/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_apollo_afd.h @brief Apollo floppy disk image (.afd) reader. */

#ifndef XXFCLIB_FORMAT_APOLLO_AFD_H
#define XXFCLIB_FORMAT_APOLLO_AFD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Apollo Domain workstation floppy image, as MAME's "apollo"
 *        floppy format reads and writes it (extension .afd).
 *
 * The file is a headerless sector dump of one fixed geometry:
 *
 *   77 cylinders x 2 heads x 8 sectors x 1024 bytes = 1,261,568 bytes,
 *   MFM, sector IDs 1..8 in physical order on every track.
 *
 * Sectors are stored cylinder by cylinder, head 0 before head 1, sector 1
 * first, so track (c, h) occupies the 8192 bytes at (c * 2 + h) * 8192.
 * Nothing else is stored: no header, no signature, no filesystem this
 * reader interprets (Domain/OS volumes are not decoded).
 *
 * Validity is the exact image size, measured from base_address to the end of
 * the device; a shorter or longer remainder is refused.  The same geometry is
 * used by PC-98 2HD, X68000 XDF, FM Towns, HP 9000/300 and RC759 images, so
 * the content cannot be told apart from theirs and the format is not
 * auto-detected: it is only reached when constructed by name.
 *
 * Each physical track becomes one STORED member of 8192 bytes, named
 * "track<cc>_<h>.bin" (cylinder two digits, head one digit), 154 members in
 * file order.  Names are generated, never read from the file.
 */
typedef struct xx_apollo_afd {
    Abstractformat format;
    uint64_t number_of_records;
} xx_apollo_afd;

typedef xx_apollo_afd xx_apollo_afd_t;

XXFC_API void xx_apollo_afd_init(xx_apollo_afd *archive, xx_io_device *device,
                                 int64_t base_address);
XXFC_API xx_apollo_afd *xx_apollo_afd_create(xx_io_device *device,
                                             int64_t base_address);
XXFC_API void xx_apollo_afd_destroy(xx_apollo_afd *archive);
XXFC_API void xx_apollo_afd_free(xx_apollo_afd *archive);

XXFC_API bool xx_apollo_afd_check_is_valid(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API bool xx_apollo_afd_handle_base_info(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API int64_t xx_apollo_afd_get_format_size(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API uint64_t xx_apollo_afd_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_apollo_afd_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_apollo_afd_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_apollo_afd_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_apollo_afd_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_apollo_afd_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_APOLLO_AFD_H */
