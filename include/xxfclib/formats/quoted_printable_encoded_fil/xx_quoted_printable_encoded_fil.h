/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_quoted_printable_encoded_fil.h
 *  @brief Quoted-Printable (RFC 2045) encoded file reader. */

#ifndef XXFCLIB_FORMAT_QUOTED_PRINTABLE_ENCODED_FIL_H
#define XXFCLIB_FORMAT_QUOTED_PRINTABLE_ENCODED_FIL_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A file that is one Quoted-Printable text (RFC 2045 section 6.7), as
 * written by Python's quopri, `qprint`, `mimencode -q`, MIME mailers and the
 * like, without any MIME headers around it.
 *
 * Grammar: printable ASCII is itself; "=XY" (two hex digits) is the byte
 * 0xXY; '=' at the end of a line (a soft line break) joins it to the next
 * line; a hard line break (LF or CRLF) is kept as it is written; spaces and
 * tabs at the end of a line were added in transport and are dropped.  There
 * is no stored size and no checksum.  The decoded data is one member named
 * "payload".
 *
 * The format has no signature, so the text is accepted only when its first
 * 64 KiB (the detection window) look like encoder output:
 *   - only printable ASCII, TAB and LF / CRLF line breaks;
 *   - no line longer than 78 characters (line break excluded; RFC 2045 says
 *     76, Python's encoders write up to 78);
 *   - no space or tab right before a line break or the end of the text;
 *   - every '=' starts "=XY" with two UPPER-case hex digits or is a soft line
 *     break ('=' right before a line break or the end of the text);
 *   - no "=XY" standing for a letter or digit, except "=46" at the start of a
 *     line (the "From " guard some encoders apply);
 *   - at least 3 pieces of evidence: soft line breaks, escapes of '=', CR,
 *     LF and bytes 0x7F..0xFF, and "=20" / "=09" ending a line (other
 *     escaped control characters are neutral);
 *   - where the text ends inside the window, only 0x1A / 0x00 padding may
 *     follow, up to EOF, at most 1 KiB of it.
 * Past the window the text ends at the first byte that is not printable
 * ASCII, TAB, CR or LF; decoding there is lenient: a malformed '=' sequence
 * is kept literally and lower-case hex digits are accepted.
 */
typedef struct xx_quoted_printable_encoded_fil {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size;  /**< Decoded payload size. */
    uint64_t escape_count;   /**< "=XY" escapes in the whole text. */
    uint64_t soft_breaks;    /**< Soft line breaks in the whole text. */
    int64_t text_size;       /**< Bytes of Quoted-Printable text. */
    int64_t format_size_all; /**< Text plus 0x1A / 0x00 padding. */
    char name[16];           /**< Member name ("payload"). */
} xx_quoted_printable_encoded_fil;

typedef xx_quoted_printable_encoded_fil xx_quoted_printable_encoded_fil_t;

XXFC_API void xx_quoted_printable_encoded_fil_init(xx_quoted_printable_encoded_fil *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_quoted_printable_encoded_fil *xx_quoted_printable_encoded_fil_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_quoted_printable_encoded_fil_destroy(xx_quoted_printable_encoded_fil *archive);
XXFC_API void xx_quoted_printable_encoded_fil_free(xx_quoted_printable_encoded_fil *archive);

XXFC_API bool xx_quoted_printable_encoded_fil_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_quoted_printable_encoded_fil_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_quoted_printable_encoded_fil_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_quoted_printable_encoded_fil_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_quoted_printable_encoded_fil_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_quoted_printable_encoded_fil_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_quoted_printable_encoded_fil_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_quoted_printable_encoded_fil_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_quoted_printable_encoded_fil_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

/** Decoded payload size; runs handle_base_info when needed, 0 on failure. */
XXFC_API uint64_t xx_quoted_printable_encoded_fil_get_unpacked_size(xx_quoted_printable_encoded_fil *archive);
/** Decode the whole payload to `destination`. */
XXFC_API bool xx_quoted_printable_encoded_fil_unpack_to_device(xx_quoted_printable_encoded_fil *archive, xx_io_device *destination, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_QUOTED_PRINTABLE_ENCODED_FIL_H */
