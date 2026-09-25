/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_installshield_12_setup.h
 *  @brief InstallShield 12-2012 single-file setup.exe (Disk1 files carried as
 *         UTF-16 records in the PE overlay) reader. */

#ifndef XXFCLIB_FORMAT_INSTALLSHIELD_12_SETUP_H
#define XXFCLIB_FORMAT_INSTALLSHIELD_12_SETUP_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An InstallShield 12..2012 "compressed" setup.exe.
 *
 * The launcher is an ordinary PE image.  Its overlay (the bytes behind the
 * last section's raw data) carries the whole Disk1 media folder:
 *
 *   u32 LE  number of files
 *   then, per file:
 *     four NUL-terminated UTF-16LE strings
 *       name      base name ("data1.cab")
 *       path      folder-relative path ("Disk1\data1.cab")
 *       version   dotted file version ("17.0.0.717", "0.0.0.0")
 *       size      decimal byte count ("566668")
 *     followed by that many bytes of the file, stored verbatim.
 *
 * The chain ends at the end of the file, or, in a signed launcher, at the
 * Authenticode certificate table the PE security directory points at (the
 * gap in between is at most the 8-byte alignment padding).
 *
 * The launcher is only parsed as far as its section table and security
 * directory; nothing in it is executed or emulated.  Members are named by
 * their path with '\' mapped to '/'.
 */
typedef struct xx_installshield_12_setup {
    Abstractformat format;
    uint64_t number_of_records; /**< Members whose data is complete. */
    uint32_t declared_count;    /**< The u32 at the start of the overlay. */
    int64_t payload_offset;     /**< Overlay start, relative to base_address. */
    int64_t payload_end;        /**< End of the last complete member. */
    int64_t certificate_offset; /**< Authenticode table, or -1. */
    int64_t certificate_size;   /**< Its size, or 0. */
    bool truncated;             /**< Fewer complete members than declared
                                     (cut off, or a damaged record). */
} xx_installshield_12_setup;

typedef xx_installshield_12_setup xx_installshield_12_setup_t;

XXFC_API void xx_installshield_12_setup_init(xx_installshield_12_setup *archive,
                                             xx_io_device *device,
                                             int64_t base_address);
XXFC_API xx_installshield_12_setup *xx_installshield_12_setup_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_installshield_12_setup_destroy(
    xx_installshield_12_setup *archive);
XXFC_API void xx_installshield_12_setup_free(xx_installshield_12_setup *archive);

XXFC_API bool xx_installshield_12_setup_check_is_valid(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API bool xx_installshield_12_setup_handle_base_info(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API int64_t xx_installshield_12_setup_get_format_size(Abstractformat *self,
                                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_installshield_12_setup_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_installshield_12_setup_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_installshield_12_setup_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_installshield_12_setup_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_installshield_12_setup_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_installshield_12_setup_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_installshield_12_setup_to_format(
    xx_installshield_12_setup *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_INSTALLSHIELD_12_SETUP_H */
