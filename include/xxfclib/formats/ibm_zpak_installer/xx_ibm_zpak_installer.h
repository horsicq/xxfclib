/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_ibm_zpak_installer.h @brief IBM "-ZPAK" self-extracting installer reader. */

#ifndef XXFCLIB_FORMAT_IBM_ZPAK_INSTALLER_H
#define XXFCLIB_FORMAT_IBM_ZPAK_INSTALLER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An IBM/Lotus "-ZPAK" self-extracting installer.
 *
 * An MZ/NE extraction stub with a private archive behind it. The archive is
 * located from the END of the file: the trailer names the file offset of an
 * eight-byte "-ZPAK" 00 + u16 version header, and every member is one PKWARE
 * DCL ("implode") stream. Version 1 keeps a 114-byte-per-member directory in
 * front of a 22-byte trailer; version 2 puts a 16-byte record and the name in
 * front of each stream and ends in a 38-byte trailer. The field tables are
 * in xx_ibm_zpak_installer.c.
 *
 * It shares the magic and the codec with IBM ZPAK (xx_ibmzpak) and nothing
 * else: that reader handles the bare archive whose header sits at offset 0.
 */
typedef struct xx_ibm_zpak_installer {
    Abstractformat format;
    uint64_t number_of_records;
    uint16_t version;          /**< 1 or 2, after handle_base_info. */
    int64_t archive_offset;    /**< Offset of the "-ZPAK" header, from base. */
} xx_ibm_zpak_installer;

typedef xx_ibm_zpak_installer xx_ibm_zpak_installer_t;

XXFC_API void xx_ibm_zpak_installer_init(xx_ibm_zpak_installer *archive,
                                         xx_io_device *device,
                                         int64_t base_address);
XXFC_API xx_ibm_zpak_installer *xx_ibm_zpak_installer_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_ibm_zpak_installer_destroy(xx_ibm_zpak_installer *archive);
XXFC_API void xx_ibm_zpak_installer_free(xx_ibm_zpak_installer *archive);

XXFC_API bool xx_ibm_zpak_installer_check_is_valid(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API bool xx_ibm_zpak_installer_handle_base_info(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API int64_t xx_ibm_zpak_installer_get_format_size(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_ibm_zpak_installer_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_ibm_zpak_installer_create_archive_records_reading(Abstractformat *self,
                                                     const xx_list_s *options,
                                                     xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ibm_zpak_installer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ibm_zpak_installer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ibm_zpak_installer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ibm_zpak_installer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IBM_ZPAK_INSTALLER_H */
