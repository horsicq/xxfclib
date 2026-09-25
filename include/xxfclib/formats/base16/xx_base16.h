/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_base16.h @brief Base16 ("hex") text-encoded data reader. */

#ifndef XXFCLIB_FORMAT_BASE16_H
#define XXFCLIB_FORMAT_BASE16_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Base16 / plain hex text: binary data written as two hexadecimal
 * digits per byte, as produced by `xxd -p`, `basenc --base16`,
 * Python's binascii.hexlify, `od`-less hex dumps and the like.
 *
 * There is no header, no stored size, no name and no checksum.  The text is
 * a sequence of hex digits (0-9, A-F, a-f), high nibble first, optionally
 * broken up by whitespace (space, TAB, CR, LF), which is ignored -- a byte's
 * two digits may even be split by a line break, as Deark's decoder allows.
 * The decoded payload is exposed as one member named "payload".
 *
 * Because nothing in the text announces the format, acceptance is a set of
 * rules over the first 64 KiB of text (the detection window):
 *   - every byte is a hex digit or whitespace, except that the text may be
 *     followed by up to 1 KiB of 0x1A / 0x00 padding that runs to EOF;
 *   - at least 8 hex digits;
 *   - both a decimal digit and a letter digit occur, and all letters have
 *     the same case;
 *   - every whitespace-separated run of digits has even length (the run the
 *     window cuts in half is exempt).
 * Past the window the text simply ends at the first byte that is neither a
 * hex digit nor whitespace; whatever follows is overlay.  A dangling final
 * nibble is not part of the format (Deark drops it too).
 */
typedef struct xx_base16 {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< Decoded payload size (digit pairs). */
    uint64_t digit_count;   /**< Hex digits inside the format. */
    int64_t text_size;      /**< Bytes of text the format spans. */
    bool is_uppercase;      /**< Letter digits are A-F rather than a-f. */
} xx_base16;

typedef xx_base16 xx_base16_t;

XXFC_API void xx_base16_init(xx_base16 *archive, xx_io_device *device,
                             int64_t base_address);
XXFC_API xx_base16 *xx_base16_create(xx_io_device *device,
                                     int64_t base_address);
XXFC_API void xx_base16_destroy(xx_base16 *archive);
XXFC_API void xx_base16_free(xx_base16 *archive);

XXFC_API bool xx_base16_check_is_valid(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API bool xx_base16_handle_base_info(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API int64_t xx_base16_get_format_size(Abstractformat *self,
                                           xx_pd_struct *pd);
XXFC_API uint64_t xx_base16_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_base16_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_base16_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_base16_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_base16_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_base16_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Decoded payload size; runs handle_base_info when needed, 0 on failure. */
XXFC_API uint64_t xx_base16_get_unpacked_size(xx_base16 *archive);
/** Decode the whole payload to `destination`. */
XXFC_API bool xx_base16_unpack_to_device(xx_base16 *archive,
                                         xx_io_device *destination,
                                         xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BASE16_H */
