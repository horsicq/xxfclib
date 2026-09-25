/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_gentee_installer.h @brief Gentee installer (gentee.com setup builder). */

#ifndef XXFCLIB_FORMAT_GENTEE_INSTALLER_H
#define XXFCLIB_FORMAT_GENTEE_INSTALLER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Gentee installer: a small PE32 stub whose overlay carries one
 * solid payload.
 *
 * The stub records where the payload starts in a 12-byte locator at file
 * offset 0x3F0 (payload offset, length of the first block, decoded size of
 * the first block).  The payload is a chain of blocks, each a u32 decoded
 * size followed by an MSB-first bit stream of an LZ77 coder over a 32 KiB
 * window driven by three adaptive Huffman trees.  Blocks carry no packed
 * length, so the chain can only be walked by decoding it.
 *
 *   block    the installer runtime (ginstall.dll); its decoder state is reset
 *   20 bytes archive header (u32 at +0 is the end of the archive in the file)
 *   2 blocks language strings and a table, not members
 *   blocks   the command chain: u16 tag, then the command body at +3.
 *            0x87F4 is a file member, 0x87F0 ends the archive.  A member's
 *            data follows its command, stored or as one block of a second,
 *            shared decoder state.
 */
typedef struct xx_gentee_installer {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t payload_offset; /**< Absolute device offset of the payload. */
    uint32_t runtime_size;  /**< Decoded size of the first block. */
    bool complete;          /**< The 0x87F0 end command was reached. */
} xx_gentee_installer;

typedef xx_gentee_installer xx_gentee_installer_t;

XXFC_API void xx_gentee_installer_init(xx_gentee_installer *archive,
                                       xx_io_device *device,
                                       int64_t base_address);
XXFC_API xx_gentee_installer *xx_gentee_installer_create(xx_io_device *device,
                                                         int64_t base_address);
XXFC_API void xx_gentee_installer_destroy(xx_gentee_installer *archive);
XXFC_API void xx_gentee_installer_free(xx_gentee_installer *archive);

XXFC_API bool xx_gentee_installer_check_is_valid(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API bool xx_gentee_installer_handle_base_info(Abstractformat *self,
                                                   xx_pd_struct *pd);
XXFC_API int64_t xx_gentee_installer_get_format_size(Abstractformat *self,
                                                     xx_pd_struct *pd);
XXFC_API uint64_t xx_gentee_installer_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_gentee_installer_create_archive_records_reading(Abstractformat *self,
                                                   const xx_list_s *options,
                                                   xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_gentee_installer_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_gentee_installer_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_gentee_installer_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_gentee_installer_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

static inline Abstractformat *xx_gentee_installer_to_format(
    xx_gentee_installer *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GENTEE_INSTALLER_H */
