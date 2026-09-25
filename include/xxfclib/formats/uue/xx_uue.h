/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_uue.h @brief UUencode / XXencode text transport reader. */

#ifndef XXFCLIB_FORMAT_UUE_H
#define XXFCLIB_FORMAT_UUE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A text file of one or more uuencoded (or XXencoded) blocks, as
 * written by `uuencode`, `xxencode`, Python's uu / binascii, Perl's
 * pack("u") and mail / news clients:
 *
 *     begin <1-6 octal digits> <name>
 *     <data line>...
 *     <zero-length line>
 *     end
 *
 * A data line is one length character (1..63 decoded bytes, 45 in practice)
 * and then 4 characters for every 3 bytes, the last group padded.  Both
 * encodings share the envelope and the line layout; only the 64-symbol
 * alphabet differs:
 *   - uuencode: 0x20..0x5F carry (c - 0x20) & 63, and '`' (0x60) is an
 *     alternative zero written by BSD / GNU encoders;
 *   - XXencode: "+-0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz".
 * There is no stored size and no checksum; every block is one member, named
 * after its header.
 *
 * Accepted layout (this reader's rules):
 *   - the first block's header sits at offset 0, optionally behind a UTF-8
 *     BOM; a header line is at most 1 KiB and holds no control byte but TAB;
 *   - lines end in LF, CRLF or a lone CR;
 *   - a data line may carry up to 2 characters past its groups (checksum
 *     characters and trailing blanks, ignored); in uuencode a line may also
 *     be short, the missing characters being blanks a mailer stripped;
 *   - a block ends at a zero-length line (length character of value 0, or an
 *     empty line) followed by an optional "end" line, at an "end" line alone,
 *     or at EOF (then it is unterminated);
 *   - each block's alphabet is chosen over its first 64 KiB: uuencode unless
 *     only XXencode parses that window without short lines.  A line that
 *     fits neither rule inside that window rejects the block; past it, the
 *     line merely ends the block (unterminated);
 *   - after a terminated block, further blocks are looked for: other text
 *     (up to 64 KiB between two blocks, no NUL or 0x1A) is skipped until the
 *     next header, and a header whose block is rejected ends the format.
 * At most 65,536 blocks are listed.
 *
 * Member names: the header name's last component (split at '/', '\\' and
 * ':'), trailing blanks dropped; bytes >= 0x80 become "%XX" and '%' becomes
 * "%25", so every name is ASCII.  A name that is empty, only dots / blanks,
 * ends in a dot, holds a control byte or one of < > " | ? *, is longer than
 * 255 bytes or is a Windows device name (CON, PRN, AUX, NUL, COM1-9,
 * LPT1-9, CONIN$, CONOUT$, CLOCK$, with or without an extension) becomes
 * "payload".  A name that repeats an earlier one (ignoring ASCII case) gets
 * "%_<block number>" inserted before its extension; "%_" occurs in no other
 * name, so renamed members never collide.
 *
 * Record metadata: XX_META_ID_ATTRIBUTES is the header mode (& 07777),
 * XX_META_ID_COMPRESSION_METHOD is 0 for uuencode and 1 for XXencode.
 */
typedef struct xx_uue {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< Sum of all members' decoded sizes. */
    int64_t text_size;      /**< Bytes the whole format spans. */
    bool is_terminated;     /**< The last block ended on its own terminator. */
} xx_uue;

typedef xx_uue xx_uue_t;

#define XX_UUE_METHOD_UU 0U
#define XX_UUE_METHOD_XX 1U

XXFC_API void xx_uue_init(xx_uue *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_uue *xx_uue_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_uue_destroy(xx_uue *archive);
XXFC_API void xx_uue_free(xx_uue *archive);

XXFC_API bool xx_uue_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_uue_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_uue_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_uue_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_uue_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_uue_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_uue_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_uue_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_uue_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Decode the current record to `destination`. */
XXFC_API bool xx_uue_unpack_current_to_device(Abstractformat *self,
                                              xx_archive_record_state *state,
                                              xx_io_device *destination,
                                              xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_UUE_H */
