/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_yenc_encoded_file.h @brief yEnc (1.2) text transport reader. */

#ifndef XXFCLIB_FORMAT_YENC_ENCODED_FILE_H
#define XXFCLIB_FORMAT_YENC_ENCODED_FILE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A text file of one or more yEnc blocks, as posted to Usenet and
 * saved by news readers (.yenc, .ntx, .txt):
 *
 *     =ybegin [part=<n> [total=<n>]] line=<n> size=<n> name=<name>
 *     [=ypart begin=<n> end=<n>]          (only with part=)
 *     <data line>...
 *     =yend size=<n> [part=<n>] [pcrc32=<hex>] [crc32=<hex>]
 *
 * A data byte is stored as (byte + 42) mod 256; NUL, LF, CR and '=' (and,
 * at an encoder's choice, any other byte) are written as '=' followed by
 * (byte + 42 + 64) mod 256.  CR and LF inside a data line are line breaks.
 *
 * Accepted layout (this reader's rules):
 *   - before the first "=ybegin " line (optionally behind a UTF-8 BOM) at
 *     most 32 KiB of text may stand (article headers); text lines hold no
 *     NUL, 0x1A or other control byte but TAB, FF and ESC;
 *   - control lines are at most 1 KiB; attributes are key=value tokens
 *     separated by blanks, keys compared without case, "name=" last and
 *     taking the rest of the line; every =ybegin needs size= and a non-empty
 *     name=, part= needs an =ypart line right after it with
 *     1 <= begin <= end <= size;
 *   - a data line is at most 16 KiB, holds no raw NUL, and never ends in a
 *     lone '='; a line starting "=ybegin " or "=ypart " inside a body
 *     rejects the block;
 *   - the decoded byte count must equal the =yend size, the declared size
 *     (single part) or end - begin + 1 (part); pcrc32 and a single part's
 *     crc32 must match;
 *   - after a block other text (up to 64 KiB, no binary byte) is skipped to
 *     the next "=ybegin " line; a block that does not parse ends the format.
 * Consecutive parts of one file (same name and size, part numbers and
 * ranges contiguous) are joined into one member, whose crc32, when the run
 * covers the whole file, must match.  A run that does not cover the whole
 * file (a lone or out-of-order part) is still listed as its own member.
 * At most 65,536 members of at most 65,536 parts each.
 *
 * check_is_valid looks for the first header within the preamble and parses
 * that block's first 64 KiB (all of it when its =yend lies inside).
 *
 * Member names: the name's last component (split at '/', '\\' and ':'),
 * one pair of surrounding double quotes and trailing blanks dropped; bytes
 * >= 0x80 become "%XX" and '%' becomes "%25".  Unsafe names (empty, only
 * dots / blanks, trailing dot, control or < > " | ? * bytes, longer than
 * 240 bytes, Windows device names incl. CONIN$, CONOUT$, CLOCK$) become
 * "payload"; a name repeating an earlier one (ignoring ASCII case) gets
 * "%_<member number>" before its extension.
 *
 * Record metadata: XX_META_ID_CRC32 is the member's CRC-32,
 * XX_META_ID_COMPRESSION_METHOD is the number of parts joined.
 */
typedef struct xx_yenc_encoded_file {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< Sum of all members' decoded sizes. */
    int64_t text_size;      /**< Bytes the whole format spans. */
} xx_yenc_encoded_file;

typedef xx_yenc_encoded_file xx_yenc_encoded_file_t;

XXFC_API void xx_yenc_encoded_file_init(xx_yenc_encoded_file *archive,
                                        xx_io_device *device,
                                        int64_t base_address);
XXFC_API xx_yenc_encoded_file *xx_yenc_encoded_file_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_yenc_encoded_file_destroy(xx_yenc_encoded_file *archive);
XXFC_API void xx_yenc_encoded_file_free(xx_yenc_encoded_file *archive);

XXFC_API bool xx_yenc_encoded_file_check_is_valid(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API bool xx_yenc_encoded_file_handle_base_info(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API int64_t xx_yenc_encoded_file_get_format_size(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API uint64_t xx_yenc_encoded_file_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_yenc_encoded_file_create_archive_records_reading(Abstractformat *self,
                                                    const xx_list_s *options,
                                                    xx_pd_struct *pd);
XXFC_API const xx_archive_record *
xx_yenc_encoded_file_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_yenc_encoded_file_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_yenc_encoded_file_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_yenc_encoded_file_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Decode the current record to `destination`. */
XXFC_API bool xx_yenc_encoded_file_unpack_current_to_device(
    Abstractformat *self, xx_archive_record_state *state,
    xx_io_device *destination, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_YENC_ENCODED_FILE_H */
