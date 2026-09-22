/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_boo.h
 *  @brief The old Usenet/BBS "boo" printable transport encoding.
 */

/* WHERE THE LAYOUT COMES FROM.  Ported from XArchive's archives/xboo.cpp,
 * whose own provenance note says the transform was read out of DEBOO.EXE -
 * the format's reference decoder, which ships BOO-encoded in this very
 * corpus.  U3's recognition predicate (FUN_006e2120, reached from VMT slot 0
 * at 0x006e26f0) agrees with it field for field and supplied the two extra
 * shape constraints noted below.
 *
 *   line 1        the payload's own DOS 8.3 file name, then CR LF or LF
 *   line 2..      payload, as printable characters
 *
 * Payload alphabet: bytes 0x30..0x6f, each carrying six bits.  Four
 * characters pack three bytes, most significant bits first:
 *
 *     out[0] = (c0 << 2) | (c1 >> 4)
 *     out[1] = (c1 << 4) | (c2 >> 2)
 *     out[2] = (c2 << 6) |  c3          (all after subtracting 0x30)
 *
 * '~' is an escape: the NEXT character, minus 0x30, is a count of NUL bytes
 * to emit.  DEBOO accepts the escape character itself in the count slot, so
 * "~~" means 0x7e - 0x30 = 78 NULs; that is the largest legal count and the
 * reason a crafted file can expand roughly 39x, which is why the decode is
 * bounded by XX_BOO_MAX_DECODED_SIZE.
 *
 * Line breaks are separators only and carry no data.  An escape may not span
 * one: DEBOO reads the count byte out of the same line buffer, so a '~' at
 * end of line is a desynchronised stream, not a run continuing onto the next
 * line.
 *
 * U3'S TWO EXTRA CONSTRAINTS, both adopted here:
 *   - the name line is a DOS 8.3 shape - at most 12 characters, at most one
 *     '.', a non-empty stem - and the whole first line must be at least three
 *     characters;
 *   - a complete payload line holds a whole number of 4-character groups.
 *     This is the single strongest discriminator against ordinary uppercase
 *     or digit-heavy prose, which otherwise sits inside the same alphabet.
 *
 * WHAT THIS READER DOES.  It publishes ONE archive record, named by the first
 * line, and really decodes it.  The decoded length is not stored anywhere, so
 * it is measured by an output-free pass of the same decoder that then
 * produces the bytes.  MAKEBOO pads the final group with NULs because the
 * stream has no length field, so at most two trailing NULs are stripped -
 * never a whole trailing NUL run, because real payloads here genuinely end in
 * hundreds of them.
 *
 * DETECTION COST.  There is no magic - the first line is the payload's name -
 * so this reader needs late dispatch.  The probe is bounded: it validates the
 * name line and then only the first XX_BOO_PROBE_WINDOW bytes of the body
 * before the full decode is attempted.
 */

#ifndef XXFCLIB_FORMAT_BOO_H
#define XXFCLIB_FORMAT_BOO_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Escape character introducing a NUL run. */
#define XX_BOO_ESCAPE 0x7eU
/** First and last character of the six-bit alphabet. */
#define XX_BOO_ALPHABET_FIRST 0x30U
#define XX_BOO_ALPHABET_LAST 0x6fU
/** Largest legal escape count ('~' itself in the count slot). */
#define XX_BOO_MAX_ESCAPE_COUNT 0x4eU
/** Longest first line this reader will scan for the name. */
#define XX_BOO_MAX_NAME_LINE 32
/** Longest name, 8.3 plus the dot. */
#define XX_BOO_MAX_NAME_SIZE 12
/** Bytes of body inspected by the cheap shape gate. */
#define XX_BOO_PROBE_WINDOW 4096
/** Smallest file and body this reader accepts. */
#define XX_BOO_MIN_FILE_SIZE 96
#define XX_BOO_MIN_BODY_SIZE 80
/** Largest encoded file read, and largest decode produced. */
#define XX_BOO_MAX_ENCODED_SIZE ((int64_t)64 * 1024 * 1024)
#define XX_BOO_MAX_DECODED_SIZE ((int64_t)256 * 1024 * 1024)

typedef struct xx_boo xx_boo;
typedef struct xx_boo xx_boo_t;
typedef struct xx_boo XBoo;

struct xx_boo {
    Abstractformat format; /**< Base format structure (first member). */
    int64_t body_offset;   /**< Absolute offset of the first payload byte. */
    int64_t unpacked_size; /**< Measured decoded length, padding removed. */
};

XXFC_API void xx_boo_init(xx_boo *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_boo *xx_boo_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_boo_destroy(xx_boo *archive);
XXFC_API void xx_boo_free(xx_boo *archive);

XXFC_API bool xx_boo_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_boo_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_boo_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_boo_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_boo_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_boo_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_boo_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_boo_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_boo_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Absolute offset of the payload, or -1 before handle_base_info. */
XXFC_API int64_t xx_boo_get_body_offset(const xx_boo *archive);
/** Decoded length, or -1 before handle_base_info. */
XXFC_API int64_t xx_boo_get_unpacked_size(const xx_boo *archive);

static inline Abstractformat *xx_boo_to_format(xx_boo *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BOO_H */
