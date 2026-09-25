/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_ardi_installer.h @brief ARDI self-extracting installer (OS/2). */

#ifndef XXFCLIB_FORMAT_ARDI_INSTALLER_H
#define XXFCLIB_FORMAT_ARDI_INSTALLER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Daniel F Valot's ARDI installer ("Ardi unpacker Version 4.22",
 * "Ardi installer Version 4.33" / "4.34").
 *
 * The carrier is a 32-bit OS/2 LX executable.  Behind its image lies a
 * chain of blocks, and the file ends in a fixed 50-byte trailer:
 *
 *   +0x00 char[25] "Copyright Daniel F Valot "
 *   +0x19 u32      builder check value
 *   +0x1D u16      builder counter
 *   +0x1F char[14] "TSHTSH - 1991-"
 *   +0x2D char[4]  year digits
 *   +0x31 char     ' '
 *
 * The chain starts with the u32 sentinel 0x98765432 and every block is
 *
 *   u32 tag, payload, u32 length   (length = 4 + payload + 4)
 *
 * so it is read backwards: the u32 just in front of the trailer is the last
 * block's length, and each tag's predecessor's length sits 4 bytes before
 * the tag, until the length slot holds the sentinel.  Tags (all LE):
 *
 *   0x12345677  member header: u32 Unix time, then "NAME!" kind code,
 *               description, NUL (the block is 14..0x100C bytes long)
 *   0x12345678  member data: a raw DEFLATE stream for the rest of the block;
 *               it always directly follows its header (ignoring the
 *               non-member blocks below)
 *   0x11221122  default destination directory, NUL terminated
 *   0x97979797  product title, NUL terminated
 *   0x12121212  installation blurb
 *   0x13131313  licence text
 *   0x98989898  four opaque builder bytes
 *
 * Any other tag is not understood and the file is refused.  The inflated
 * size of a member is recorded nowhere.  The trailer alone is not a
 * signature: the author stamps it on his ordinary programs too, so a file
 * is accepted only when the whole chain walks back to the sentinel.
 */
typedef struct xx_ardi_installer {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t chain_offset;   /**< The 0x98765432 sentinel, from base. */
    uint32_t block_count;   /**< Blocks between the sentinel and the trailer. */
    char year[5];           /**< The trailer's year digits. */
    char title[256];        /**< Product title (UTF-8), "" if none. */
    char install_path[512]; /**< Default destination (UTF-8), "" if none. */
} xx_ardi_installer;

typedef xx_ardi_installer xx_ardi_installer_t;

XXFC_API void xx_ardi_installer_init(xx_ardi_installer *archive,
                                     xx_io_device *device,
                                     int64_t base_address);
XXFC_API xx_ardi_installer *xx_ardi_installer_create(xx_io_device *device,
                                                     int64_t base_address);
XXFC_API void xx_ardi_installer_destroy(xx_ardi_installer *archive);
XXFC_API void xx_ardi_installer_free(xx_ardi_installer *archive);

XXFC_API bool xx_ardi_installer_check_is_valid(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API bool xx_ardi_installer_handle_base_info(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API int64_t xx_ardi_installer_get_format_size(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API uint64_t xx_ardi_installer_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_ardi_installer_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_ardi_installer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_ardi_installer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_ardi_installer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_ardi_installer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ARDI_INSTALLER_H */
