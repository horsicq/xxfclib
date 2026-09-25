/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_installshield_developer.h
 *  @brief InstallShield Developer 7 Setup Launcher (setup.exe) payload reader. */

#ifndef XXFCLIB_FORMAT_INSTALLSHIELD_DEVELOPER_H
#define XXFCLIB_FORMAT_INSTALLSHIELD_DEVELOPER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An InstallShield Developer 7 (ISWI 7.0) InstallScript-MSI Setup
 *        Launcher: a PE32 stub with a stored support-file table appended at
 *        the exact end of its last section (the PE overlay).
 *
 * Payload, at the overlay offset:
 *
 *   0x00  char[14] "InstallShield\0"
 *   0x0E  u32 LE   number of files, 1..65535
 *   0x12  26 zero bytes, up to 0x2E
 *
 * then that many records, each a fixed 0x138-byte header immediately
 * followed by the file's bytes, stored:
 *
 *   0x000 char[]   file name, ANSI, NUL terminated; every byte after the
 *                  terminator up to 0x10C is zero (later launcher builds
 *                  put encoding flags in 0x104..0x10B; this variant has
 *                  none, and a record carrying them is not accepted)
 *   0x10C u32 LE   file size
 *   0x110 40 zero bytes, up to 0x138
 *
 * In an intact launcher the chain ends exactly at the end of the file, or
 * at an Authenticode certificate table that runs to the end of the file.
 * The members are the launcher's own support files: 0xLLLL.ini string
 * tables, instmsiw.exe / instmsia.exe, isscript.msi, NNNN.mst transforms
 * and Setup.ini.
 */
typedef struct xx_installshield_developer {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t payload_offset;  /**< Overlay offset, relative to base_address. */
    int64_t payload_end;     /**< End of the last complete member. */
    uint32_t declared_count; /**< The file count in the payload header. */
    bool damaged; /**< The chain stops early: truncated or corrupt tail. */
    bool has_certificate; /**< A certificate table follows the chain. */
} xx_installshield_developer;

typedef xx_installshield_developer xx_installshield_developer_t;

/** Size of the payload header at the overlay. */
#define XX_INSTALLSHIELD_DEVELOPER_HEADER_SIZE 0x2E
/** Size of one member record header. */
#define XX_INSTALLSHIELD_DEVELOPER_RECORD_SIZE 0x138

XXFC_API void xx_installshield_developer_init(
    xx_installshield_developer *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_installshield_developer *xx_installshield_developer_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_installshield_developer_destroy(
    xx_installshield_developer *archive);
XXFC_API void xx_installshield_developer_free(
    xx_installshield_developer *archive);

XXFC_API bool xx_installshield_developer_check_is_valid(Abstractformat *self,
                                                        xx_pd_struct *pd);
XXFC_API bool xx_installshield_developer_handle_base_info(Abstractformat *self,
                                                          xx_pd_struct *pd);
XXFC_API int64_t xx_installshield_developer_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_installshield_developer_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_installshield_developer_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_installshield_developer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_installshield_developer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_installshield_developer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_installshield_developer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INSTALLSHIELD_DEVELOPER_H */
