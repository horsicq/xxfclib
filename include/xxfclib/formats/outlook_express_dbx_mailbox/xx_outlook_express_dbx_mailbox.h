/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_outlook_express_dbx_mailbox.h
 *  @brief Outlook Express 5/6 DBX message store reader. */

#ifndef XXFCLIB_FORMAT_OUTLOOK_EXPRESS_DBX_MAILBOX_H
#define XXFCLIB_FORMAT_OUTLOOK_EXPRESS_DBX_MAILBOX_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An Outlook Express message store (one mail or news folder).
 *
 * All offsets are absolute file offsets, all integers little-endian.
 *
 * File header, 0x24BC bytes:
 *   0x0000  CF AD 12 FE
 *   0x0004  16-byte class id; message stores carry
 *           C5 FD 74 6F 66 E3 D1 11 9A 4E 00 C0 4F D7 5E 5B
 *           (Folders.dbx has C6 in the first byte and holds no messages)
 *   0x00C4  u32 number of entries in the message tree
 *   0x00E4  u32 offset of the root node of the message tree (0 = empty)
 *
 * Tree node, 0x27C bytes (a B-tree, walked in order):
 *   +0x00 u32 own offset   +0x08 u32 leftmost child node
 *   +0x0C u32 parent node (checked: dbxplug also rejects a child whose
 *         parent field is wrong)
 *   +0x11 u8  entries used (<= 0x33)
 *   +0x14 u32 values below the leftmost child (not needed for reading)
 *   +0x18 entries of 12 bytes: u32 info object, u32 right child node,
 *         u32 values below that child
 *
 * Info object:
 *   +0x00 u32 own offset   +0x04 u32 body length
 *   +0x0A u8  attribute count, then that many u32 attributes:
 *         low byte = attribute id (bit 7: value is stored directly),
 *         upper 24 bits = the value, or its offset into the data area that
 *         follows the attribute array.  Attribute 0x04 is the file offset of
 *         the message's first block; 0x08 is the subject.
 *
 * Message block: +0x00 u32 own offset, +0x04 u32 block body size (0x200),
 *   +0x08 u32 bytes used in this body, +0x0C u32 next block (0 = last),
 *   then the body.  The message is the concatenation of the used bytes: the
 *   raw RFC 822 text, extracted as <index>.eml.
 */
typedef struct xx_outlook_express_dbx_mailbox_message {
    uint32_t info_offset;
    uint32_t first_block;
    uint64_t size;
    bool broken; /**< The block chain leaves the file, loops or is corrupt. */
} xx_outlook_express_dbx_mailbox_message;

typedef struct xx_outlook_express_dbx_mailbox {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t declared_count; /**< u32 at 0xC4. */
    uint32_t root_node;      /**< u32 at 0xE4. */
    xx_outlook_express_dbx_mailbox_message *messages;
    size_t message_count;
    bool parsed;
} xx_outlook_express_dbx_mailbox;

typedef xx_outlook_express_dbx_mailbox xx_outlook_express_dbx_mailbox_t;

#define XX_OUTLOOK_EXPRESS_DBX_MAILBOX_HEADER_SIZE 0x24BC

XXFC_API void xx_outlook_express_dbx_mailbox_init(
    xx_outlook_express_dbx_mailbox *archive, xx_io_device *device,
    int64_t base_address);
XXFC_API xx_outlook_express_dbx_mailbox *xx_outlook_express_dbx_mailbox_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_outlook_express_dbx_mailbox_destroy(
    xx_outlook_express_dbx_mailbox *archive);
XXFC_API void xx_outlook_express_dbx_mailbox_free(
    xx_outlook_express_dbx_mailbox *archive);

XXFC_API bool xx_outlook_express_dbx_mailbox_check_is_valid(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_outlook_express_dbx_mailbox_handle_base_info(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_outlook_express_dbx_mailbox_get_format_size(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_outlook_express_dbx_mailbox_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_outlook_express_dbx_mailbox_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_outlook_express_dbx_mailbox_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_outlook_express_dbx_mailbox_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_outlook_express_dbx_mailbox_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_outlook_express_dbx_mailbox_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** @brief Write message @p index (tree order) to @p destination; NULL only
 *  verifies the block chain. */
XXFC_API bool xx_outlook_express_dbx_mailbox_unpack_message(
    xx_outlook_express_dbx_mailbox *archive, size_t index,
    xx_io_device *destination, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_OUTLOOK_EXPRESS_DBX_MAILBOX_H */
