/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_arni_installer_container.h
 *  @brief ARNI installer container (the mIRC setup stub's "ARNI" chain). */

#ifndef XXFCLIB_FORMAT_ARNI_INSTALLER_CONTAINER_H
#define XXFCLIB_FORMAT_ARNI_INSTALLER_CONTAINER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An ARNI installer: the mIRC setup executables of 1998..2001.
 *
 * The carrier is a PE executable and the container is NOT in its overlay:
 * it is the whole contents of one RCDATA resource (named "MIRCALL" in some
 * builds, numbered 4 in others).  The container has no header, no count, no
 * directory and no names; it is a flat chain of records
 *
 *   0x00  char[4]  "ARNI"       record tag
 *   0x04  i32 LE   decoded size 0 < n < 0x1000000
 *   0x08  packed bytes          Yoshizaki LZHUF, no stored packed length
 *
 * closed by a ten-byte end record, "ARNI" "ARNI" 0D 0A.  A record does not
 * store its packed length: the next record's tag starts on the byte after
 * the stream, so a member's packed extent is the distance to the next header.
 *
 * Every member is a plain LZHUF stream (F = 0x3C, THRESHOLD = 2, 0x2000-byte
 * ring preset to 0x20, no end symbol), decoded by xx_lzhuf_decode_memory().
 *
 * Member names are not in the container.  The stub keeps the bare file names
 * it installs, in record order, as a pool of NUL-terminated strings in its
 * data; when the bytes ahead of the container hold exactly one such run whose
 * length is the member count (and at least three), those names are
 * published.  The DLL names the import descriptors point at are not counted
 * as pool entries.  Otherwise member i is "File_<i>.bin", as the references
 * do.
 */
typedef struct xx_arni_installer_container {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t container_offset; /**< The RCDATA data, relative to base_address. */
    int64_t container_size;   /**< Size of that resource. */
    int64_t chain_end;        /**< End of the end record, relative to base. */
    uint64_t unpacked_total;  /**< Sum of the members' decoded sizes. */
    bool names_recovered;     /**< True when the stub's name pool was used. */
    void *table;              /**< Member table cached by handle_base_info. */
} xx_arni_installer_container;

typedef xx_arni_installer_container xx_arni_installer_container_t;

/** Record tag plus decoded-size dword. */
#define XX_ARNI_INSTALLER_CONTAINER_HEADER_SIZE 8
/** "ARNIARNI\r\n". */
#define XX_ARNI_INSTALLER_CONTAINER_END_SIZE 10
/** Exclusive upper bound of the decoded-size field. */
#define XX_ARNI_INSTALLER_CONTAINER_MAX_MEMBER 0x1000000

/** Compression method value published in XX_META_ID_COMPRESSION_METHOD. */
#define XX_ARNI_INSTALLER_CONTAINER_METHOD_LZHUF 1U

XXFC_API void xx_arni_installer_container_init(
    xx_arni_installer_container *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_arni_installer_container *xx_arni_installer_container_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_arni_installer_container_destroy(
    xx_arni_installer_container *archive);
XXFC_API void xx_arni_installer_container_free(
    xx_arni_installer_container *archive);

XXFC_API bool xx_arni_installer_container_check_is_valid(Abstractformat *self,
                                                         xx_pd_struct *pd);
XXFC_API bool xx_arni_installer_container_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_arni_installer_container_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_arni_installer_container_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_arni_installer_container_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_arni_installer_container_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_arni_installer_container_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_arni_installer_container_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_arni_installer_container_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ARNI_INSTALLER_CONTAINER_H */
