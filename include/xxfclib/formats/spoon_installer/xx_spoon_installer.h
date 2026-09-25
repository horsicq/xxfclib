/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_spoon_installer.h @brief Spoon Installer self-extractor reader. */

#ifndef XXFCLIB_FORMAT_SPOON_INSTALLER_H
#define XXFCLIB_FORMAT_SPOON_INSTALLER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Spoon Installer executable (Windows, ca. 2002-2005).
 *
 * A PE stub ("Spoon Installer Language :" among its strings, 0x28200 bytes
 * in every known build) followed by one complete bzip2 stream per installed
 * file, back to back, then a file directory and a fixed footer that ends the
 * file.  All offsets are little endian and count from the start of the
 * executable.
 *
 * Footer, the last 24 bytes:
 *   +0x00  u32   unknown (differs per installer)
 *   +0x04  u32   unknown (differs per installer)
 *   +0x08  u32   directory offset
 *   +0x0C  u32   number of files
 *   +0x10  u8[8] 83 52 34 03 43 F3 FF 0E
 *
 * Directory, from its offset up to the footer, one record per file:
 *   +0x00  u32   stream offset
 *   +0x04  u32   stream (packed) size
 *   +0x08  u32   unpacked size
 *   +0x0C  u32   sum of the packed stream's bytes, modulo 2^32
 *   +0x10  u8    name length N, counting the terminating NUL
 *   +0x11  N     name (ANSI), NUL terminated
 *
 * The first stream starts where the stub ends; each next one starts where
 * the previous one ends, and the directory starts where the last one ends.
 * The builder stores "Setup.bmp" (the banner) first and "Setup.dat" (the
 * install script) last; the payload files are named "<id> - <file name>"
 * and Setup.dat maps them to their destinations.
 */
typedef struct xx_spoon_installer {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t payload_offset;   /**< Device offset of the first stream. */
    int64_t directory_offset; /**< Device offset of the directory. */
    int64_t directory_size;
    uint64_t unpacked_total;  /**< Sum of the declared unpacked sizes. */
    uint32_t footer_value0;   /**< Footer +0x00, meaning unknown. */
    uint32_t footer_value1;   /**< Footer +0x04, meaning unknown. */
} xx_spoon_installer;

typedef xx_spoon_installer xx_spoon_installer_t;

#define XX_SPOON_INSTALLER_FOOTER_SIZE 24
#define XX_SPOON_INSTALLER_SIGNATURE_SIZE 8

XXFC_API void xx_spoon_installer_init(xx_spoon_installer *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_spoon_installer *xx_spoon_installer_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_spoon_installer_destroy(xx_spoon_installer *archive);
XXFC_API void xx_spoon_installer_free(xx_spoon_installer *archive);

XXFC_API bool xx_spoon_installer_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_spoon_installer_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_spoon_installer_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_spoon_installer_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_spoon_installer_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_spoon_installer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_spoon_installer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_spoon_installer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_spoon_installer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SPOON_INSTALLER_H */
