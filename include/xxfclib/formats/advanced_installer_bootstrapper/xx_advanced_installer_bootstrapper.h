/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_advanced_installer_bootstrapper.h
 *  @brief Advanced Installer (Caphyon) EXE bootstrapper reader. */

/*
 * An Advanced Installer "EXE setup" is a PE stub with its payload appended
 * as overlay and a 74-byte trailer at the very end of the file (or, for a
 * signed bootstrapper, just in front of the Authenticode certificate table,
 * followed by at most seven zero bytes of alignment):
 *
 *   footer, 64 bytes, all u32 little endian:
 *     +0x00  mode             0: payload inside; 1: the MSI is an external
 *                             sibling file named at +0x04
 *     +0x04  external_name    mode 0: the footer offset; mode 1: offset of
 *                             {u32 0, u32 n, UTF-16LE name[n]} ending at
 *                             the footer
 *     +0x08  file_count       records in the file table
 *     +0x0C  version          100
 *     +0x10  metadata_end     the footer's own offset
 *     +0x14  info_offset      start of the file table
 *     +0x18  data_offset      start of the stored files (the PE overlay)
 *     +0x1C  char[32]         hex digits (a GUID without punctuation)
 *     +0x3C  u32              not interpreted
 *   "ADVINSTSFX"              10-byte marker
 *
 *   file table at info_offset, file_count records of
 *     u32 type, u32 index, u32 xor_flag, u32 size, u32 offset, u32 name_len
 *     followed by name_len UTF-16LE code units (no terminator; '\' separates
 *     directories).  The table ends exactly at the footer (mode 0) or at
 *     the external name block (mode 1).
 *
 * Every offset is absolute from the start of the executable.  Files are
 * stored; xor_flag 2 means the first min(size, 0x200) bytes are XORed with
 * 0xFF, xor_flag 0 means plain.  Typical members are the language resource
 * DLLs, the 7z decoder DLL, the MSI (or a 7z holding it), CABs, a FILES.7z
 * and the bootstrapper's .ini.  Members are extracted as stored (after the
 * XOR is undone); nested MSI / CAB / 7z payloads are left to their own
 * readers.  Mode 1 bootstrappers list their table (normally only the .ini);
 * the external MSI they name is reported by
 * xx_advanced_installer_bootstrapper_get_external_name() and never opened.
 *
 * The executable is parsed only as far as needed to find the trailer: the
 * DOS header, the PE signature and the security data directory.
 */

#ifndef XXFCLIB_FORMAT_ADVANCED_INSTALLER_BOOTSTRAPPER_H
#define XXFCLIB_FORMAT_ADVANCED_INSTALLER_BOOTSTRAPPER_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_MARKER "ADVINSTSFX"
#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_MARKER_SIZE 10U
#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_FOOTER_SIZE 64U
#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_VERSION 100U
#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_RECORD_SIZE 24U
#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_MAX_FILES 4096U
#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_MAX_NAME 2048U
#define XX_ADVANCED_INSTALLER_BOOTSTRAPPER_XOR_SIZE 0x200U

typedef struct xx_advanced_installer_bootstrapper {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t mode;             /**< 0 payload inside, 1 external MSI. */
    uint32_t info_offset;      /**< File table, from the executable start. */
    uint32_t data_offset;      /**< First stored file. */
    int64_t footer_offset;     /**< Absolute device offset of the footer. */
    bool is_signed;            /**< Trailer sits before a certificate table. */
    bool is_pe64;
    char guid[33];             /**< The 32 hex digits, NUL terminated. */
    char *external_name;       /**< Mode 1 only: UTF-8, owned. */
} xx_advanced_installer_bootstrapper;

typedef xx_advanced_installer_bootstrapper xx_advanced_installer_bootstrapper_t;

XXFC_API void xx_advanced_installer_bootstrapper_init(
    xx_advanced_installer_bootstrapper *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_advanced_installer_bootstrapper *
xx_advanced_installer_bootstrapper_create(xx_io_device *device,
                                          int64_t base_address);
XXFC_API void xx_advanced_installer_bootstrapper_destroy(
    xx_advanced_installer_bootstrapper *archive);
XXFC_API void xx_advanced_installer_bootstrapper_free(
    xx_advanced_installer_bootstrapper *archive);

XXFC_API bool xx_advanced_installer_bootstrapper_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_advanced_installer_bootstrapper_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_advanced_installer_bootstrapper_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_advanced_installer_bootstrapper_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_advanced_installer_bootstrapper_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_advanced_installer_bootstrapper_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_advanced_installer_bootstrapper_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_advanced_installer_bootstrapper_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_advanced_installer_bootstrapper_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** 0 or 1 after handle_base_info. */
XXFC_API uint32_t xx_advanced_installer_bootstrapper_get_mode(
    const xx_advanced_installer_bootstrapper *archive);
/** The footer's 32 hex digits, or NULL before handle_base_info. */
XXFC_API const char *xx_advanced_installer_bootstrapper_get_guid(
    const xx_advanced_installer_bootstrapper *archive);
/** Mode 1: the external MSI name (UTF-8) the footer names, else NULL. */
XXFC_API const char *xx_advanced_installer_bootstrapper_get_external_name(
    const xx_advanced_installer_bootstrapper *archive);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ADVANCED_INSTALLER_BOOTSTRAPPER_H */
