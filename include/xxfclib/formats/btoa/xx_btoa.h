/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_btoa.h @brief btoa / xbtoa and Adobe Ascii85 transport reader. */

#ifndef XXFCLIB_FORMAT_BTOA_H
#define XXFCLIB_FORMAT_BTOA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Base-85 ("Ascii85") text transport encodings.
 *
 * Every variant codes four binary bytes, taken as a big-endian 32-bit value,
 * as five base-85 digits written most significant first, each digit plus
 * 0x21 ('!'..'u').  'z' stands alone, between groups, for four zero bytes.
 * A group above 0xFFFFFFFF cannot be written by any encoder and is refused.
 *
 * btoa 4.x ("xbtoa Begin") and btoa 5.x ("xbtoa5 <width> <name> Begin"):
 *
 *   a header line, the body, then
 *   "xbtoa End N <length> <length in hex> E <eor> S <sum> R <rot>"
 *
 *   The encoder pads the input with zero bytes to whole four-byte groups,
 *   so the body always holds whole groups and `N` says how much of the last
 *   one is real; both spellings of `N` must agree.  The three checksums are
 *     E  XOR of every item
 *     S  sum of (item + 1) over every item
 *     R  accumulator shifted left by one, plus the bit shifted out of
 *        bit 31, plus the item
 *   In the old format an item is a decoded byte, padding included.  In the
 *   5.x format an item is a data CHARACTER of the body (the text, not the
 *   binary), 'z' / 'y' counted as one character each.
 *   btoa keeps S and R in a C `long`: a 32-bit build prints them wrapped at
 *   32 bits, an LP64 build prints 64-bit values whose low 32 bits are the
 *   same.  Either form is accepted.
 *
 *   Old format: line breaks carry no meaning; SP and TAB are ignored.
 *   Shorthands, all only between groups, are resolved by the checksum:
 *     'y'  four bytes 0x20 (btoa 4.2) or four bytes 0xFF (the DOS "b2a"
 *          encoder); both readings are checksummed and the matching one
 *          is used
 *     'w'  256 bytes 0xFF (b2a)
 *   5.x format: every body line ends with one check character that is not
 *   data; every line but the last is exactly <width> characters long;
 *   'y' is four bytes 0x20.  The header's name is reduced to its last path
 *   component and used as the member name when it is safe.
 *
 *   Begin..End segments that follow each other (blank lines between them
 *   allowed) are one record each, up to 1024 records.  A segment decodes
 *   to at most 256 MiB and the listed segments to at most 1 GiB together
 *   (the 'w' shorthand expands one character to 256 bytes).
 *
 * Adobe Ascii85 ("<~" ... "~>"): no header, length or checksum.  White
 * space (NUL, TAB, LF, FF, CR, SP) is ignored, a final group of k (2..4)
 * digits is padded with 'u' and yields k-1 bytes, and "~>" ends the data.
 * It is accepted only when every byte up to "~>" belongs to the grammar and
 * at least one byte decodes.  The stream ends at "~>"; one record.
 *
 * Member names: "payload" for the old format and Adobe, the 5.x header's
 * leaf name when it is a safe file name (else "payload"); a name an earlier
 * record already uses (ignoring case) gets "_<record number>".
 */
typedef struct xx_btoa {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t unpacked_size; /**< Sum of the records' decoded sizes. */
    uint32_t variant;       /**< XX_BTOA_VARIANT_* of the first record. */
} xx_btoa;

typedef xx_btoa xx_btoa_t;

#define XX_BTOA_VARIANT_OLD 1U   /**< "xbtoa Begin" (btoa 4.x, b2a). */
#define XX_BTOA_VARIANT_NEW 2U   /**< "xbtoa5 ..." (btoa 5.x). */
#define XX_BTOA_VARIANT_ADOBE 3U /**< "<~" ... "~>". */

XXFC_API void xx_btoa_init(xx_btoa *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_btoa *xx_btoa_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_btoa_destroy(xx_btoa *archive);
XXFC_API void xx_btoa_free(xx_btoa *archive);

XXFC_API bool xx_btoa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_btoa_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_btoa_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_btoa_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_btoa_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_btoa_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_btoa_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_btoa_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_btoa_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BTOA_H */
