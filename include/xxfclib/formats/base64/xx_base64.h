/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_base64.h @brief Base64 / MIME text-encoded data reader. */

#ifndef XXFCLIB_FORMAT_BASE64_H
#define XXFCLIB_FORMAT_BASE64_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A file that is one Base64 text (RFC 4648 alphabet A-Z a-z 0-9 + /,
 * '=' padding), as written by `base64`, `openssl base64`, MIME encoders,
 * Python's base64 module and the like, or the same text wrapped in the
 * sharutils envelope that `uuencode -m` / `b64encode` write:
 *
 *     begin-base64 <1-4 octal digits> <name>
 *     <Base64 lines>
 *     ====
 *
 * Four symbols carry three bytes; a final group of two or three symbols
 * carries one or two bytes and is normally padded to four with '='.  CR and
 * LF between symbols are ignored when decoding.  There is no stored size and
 * no checksum.  The decoded data is one member, named after the envelope's
 * <name> when that is a safe plain file name, else "payload".
 *
 * Bare Base64 has no signature, so a bare text is accepted only when the
 * first 64 KiB of it (the detection window) looks like encoder output:
 *   - only symbols, '=' padding and LF / CRLF line breaks, no blank line
 *     inside the text;
 *   - every line but the last has the same width, a multiple of 4 and at
 *     least 16, and the last line is no longer (a single line is fine);
 *   - at least 16 symbols, both letter cases, a symbol outside the hex
 *     digits, at least 24 different symbols once there are 256 and 32 once
 *     there are 1024, and below 64 symbols a digit, '+', '/' or '='
 *     padding;
 *   - where the text ends inside the window it ends on a whole group or on
 *     correct padding, and only whitespace, then 0x1A / 0x00 padding, may
 *     follow to EOF (at most 1 KiB of it past the window).
 * Past the window the text ends at the padding, at a blank line or at the
 * first byte that is neither a symbol nor a line break.
 *
 * The envelope is its own evidence: its header must sit at offset 0, its
 * body lines hold only symbols, padding and line breaks up to the "===="
 * line (or EOF), and whatever follows that line is overlay.
 */
typedef struct xx_base64 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< Decoded payload size. */
    uint64_t symbol_count;  /**< Base64 symbols in the text (no padding). */
    int64_t data_offset;    /**< First byte of Base64 text (after a header). */
    int64_t data_size;      /**< Bytes from data_offset to the last symbol
                                 or pad. */
    int64_t text_size;      /**< Bytes the whole format spans. */
    uint32_t line_width;    /**< Width of the first full line; 0 = one line. */
    uint32_t mode;          /**< Octal mode from the envelope, else 0. */
    bool is_wrapped;        /**< Has a "begin-base64" envelope. */
    bool is_terminated;     /**< The envelope's "====" line was found. */
    char name[256];         /**< Member name. */
} xx_base64;

typedef xx_base64 xx_base64_t;

XXFC_API void xx_base64_init(xx_base64 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_base64 *xx_base64_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_base64_destroy(xx_base64 *archive);
XXFC_API void xx_base64_free(xx_base64 *archive);

XXFC_API bool xx_base64_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_base64_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_base64_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_base64_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_base64_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_base64_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_base64_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_base64_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_base64_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Decoded payload size; runs handle_base_info when needed, 0 on failure. */
XXFC_API uint64_t xx_base64_get_unpacked_size(xx_base64 *archive);
/** Decode the whole payload to `destination`. */
XXFC_API bool xx_base64_unpack_to_device(xx_base64 *archive,
                                         xx_io_device *destination,
                                         xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BASE64_H */
