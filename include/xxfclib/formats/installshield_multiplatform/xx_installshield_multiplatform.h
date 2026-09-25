/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_installshield_multiplatform.h
 *  @brief InstallShield MultiPlatform (ISMP / Java Edition) native launcher. */

#ifndef XXFCLIB_FORMAT_INSTALLSHIELD_MULTIPLATFORM_H
#define XXFCLIB_FORMAT_INSTALLSHIELD_MULTIPLATFORM_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An ISMP native launcher ("setup.exe", "setuplinux.bin",
 *        "setupaix.bin", ...) and the resources appended to it.
 *
 * The launcher itself is an ordinary PE, ELF or XCOFF executable.  The
 * builder appends its resources, stored raw one after the other, then an
 * index and an 8-byte footer that ends the file.  Every multi-byte field is
 * big-endian (the builder is Java and writes with a DataOutputStream):
 *
 *   footer, the last 8 bytes of the file
 *     +0  u32  index offset, from the start of the launcher
 *     +4  u32  0xCA82CA82
 *
 *   index, at the index offset
 *     u32  number of entries
 *     per entry
 *       u8   resource type   0 instructions.txt   1 JVM description (*.jvm)
 *                            2 launch.txt         4 bundled JVM
 *                            5 Verify.jar         6 setup.jar
 *       u32  resource id     0, 1, 2, ... in index order
 *       u32  size
 *       u32  offset, from the start of the launcher
 *       u16  name length, then the name in Java modified UTF-8
 *       u8   0 or 1: whether a u64 follows (only bundled JVMs carry one)
 *     one byte (0 in every launcher measured), then the footer; the reader
 *     does not interpret the bytes between the last entry and the footer
 *
 * The resources lie between the end of the executable image and the index;
 * in every measured launcher they are contiguous and the last one ends
 * exactly at the index offset.  The reader parses nothing of the
 * executable itself.
 */
typedef struct xx_installshield_multiplatform {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t index_offset;  /**< From the base address; -1 until parsed. */
    int64_t index_size;    /**< Index start to end of file, footer included. */
} xx_installshield_multiplatform;

typedef xx_installshield_multiplatform xx_installshield_multiplatform_t;

/** Footer magic, as a big-endian u32 in the last 4 bytes of the file. */
#define XX_INSTALLSHIELD_MULTIPLATFORM_MAGIC 0xCA82CA82U
#define XX_INSTALLSHIELD_MULTIPLATFORM_FOOTER_SIZE 8

XXFC_API void xx_installshield_multiplatform_init(
    xx_installshield_multiplatform *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_installshield_multiplatform *xx_installshield_multiplatform_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_installshield_multiplatform_destroy(
    xx_installshield_multiplatform *archive);
XXFC_API void xx_installshield_multiplatform_free(
    xx_installshield_multiplatform *archive);

XXFC_API bool xx_installshield_multiplatform_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_installshield_multiplatform_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_installshield_multiplatform_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_installshield_multiplatform_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_installshield_multiplatform_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_installshield_multiplatform_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_installshield_multiplatform_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_installshield_multiplatform_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_installshield_multiplatform_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INSTALLSHIELD_MULTIPLATFORM_H */
