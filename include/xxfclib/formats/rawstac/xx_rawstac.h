/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_rawstac.h
 *  @brief Stac Electronics LZS stream with NO container header at all.
 */

/* WHAT THIS IS.  The same codec as the "sTaC" container (see xx_stac.h), but
 * the four magic bytes are absent: the file IS the bit stream, from offset 0.
 * The corpus (F:\ARC\ARC\RAW STAC) is nine such files - four .ECC, four
 * SYTOS.LOG and one OPPLUS.HLP - and their first byte is already the first
 * LZS token.
 *
 * HOW U3 RECOGNISES IT, AND WHY THIS READER DOES NOT COPY THAT.  U3's Raw
 * STAC predicate (FUN_0060bdb0 -> FUN_0060bb90 -> FUN_0060bb40) is a loop of
 * exactly three 16-byte comparisons against a table at 0x00827fd4: it knows
 * three specific files by their first sixteen bytes and nothing else.  That
 * matches this corpus exactly - it contains exactly three distinct 16-byte
 * prefixes - which is strong evidence that the table really is three
 * hard-coded prefixes rather than a rule.  A table of three literals is not a
 * format test; it recognises three files and rejects every other raw LZS
 * stream in the world, and it cannot be ported as a "detection rule" without
 * pretending to knowledge nobody has.
 *
 * So the detector here is the DECODE ITSELF, which is both more general and
 * much harder to fool:
 *
 *   - the stream must parse as LZS from bit 0 with no token left over;
 *   - it must terminate on the LZS end marker (1 1 0000000), not by running
 *     out of input;
 *   - the end marker must fall in the LAST byte of the file, i.e. the decoder
 *     consumes ceil(bits/8) == file size.  A raw stream has no length field,
 *     so "the terminator is exactly at EOF" is the only structural statement
 *     the file makes about itself, and it is the one this reader verifies;
 *   - the result must actually be a compression: decoded size >= encoded
 *     size, and at least XX_RAWSTAC_MIN_DECODED_SIZE bytes;
 *   - the input must be at least XX_RAWSTAC_MIN_PACKED_SIZE bytes, because a
 *     handful of bytes will satisfy any bit grammar by accident.
 *
 * All nine corpus samples pass: 2978, 2978, 2975, 2975, 261117, 16030, 16010,
 * 16030 and 16010 bytes in, each consumed to the last byte, decoding to 4029,
 * 4029, 4029, 4029, 365781, 26952, 26952, 26952 and 26952 bytes, and every
 * decoded image starts with a real header ("MZ" for eight of them, "HSP" for
 * OPPLUS.HLP).  Hitting the end marker precisely at EOF nine times out of nine
 * is not something a wrong grammar does.
 *
 * COST AND DISPATCH.  There is no magic, so this reader CANNOT be put behind a
 * cheap byte prefilter and must be dispatched late, after every format that
 * has a signature has been tried.  The validity test reads and decodes the
 * whole file, which is why XX_RAWSTAC_MAX_PACKED_SIZE caps what it will look
 * at; beyond that it declines rather than spending unbounded time on a
 * speculative probe.
 *
 * ONE HONEST LIMITATION.  Because detection is "it decodes", a raw LZS stream
 * produced by some other tool would also be reported as Raw STAC.  That is
 * the right trade: the format genuinely has no other identity.
 */

#ifndef XXFCLIB_FORMAT_RAWSTAC_H
#define XXFCLIB_FORMAT_RAWSTAC_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Smallest stream this reader will consider.  Short bit strings satisfy the
 *  LZS grammar by chance, so anything below this is refused outright. */
#define XX_RAWSTAC_MIN_PACKED_SIZE 256
/** Smallest decoded result accepted. */
#define XX_RAWSTAC_MIN_DECODED_SIZE 256
/** Largest input this speculative probe will read and decode. */
#define XX_RAWSTAC_MAX_PACKED_SIZE ((int64_t)64 * 1024 * 1024)
/** Hard ceiling on a decoded member. */
#define XX_RAWSTAC_MAX_DECODED_SIZE ((int64_t)256 * 1024 * 1024)

typedef struct xx_rawstac xx_rawstac;
typedef struct xx_rawstac xx_rawstac_t;
typedef struct xx_rawstac XRawStac;

struct xx_rawstac {
    Abstractformat format; /**< Base format structure (first member). */
    int64_t packed_size;   /**< Bytes consumed by the decode; equals the file. */
    int64_t unpacked_size; /**< Measured decoded length. */
};

XXFC_API void xx_rawstac_init(xx_rawstac *archive, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_rawstac *xx_rawstac_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_rawstac_destroy(xx_rawstac *archive);
XXFC_API void xx_rawstac_free(xx_rawstac *archive);

XXFC_API bool xx_rawstac_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_rawstac_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_rawstac_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_rawstac_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_rawstac_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_rawstac_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_rawstac_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_rawstac_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_rawstac_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Packed length, or -1 before handle_base_info. */
XXFC_API int64_t xx_rawstac_get_packed_size(const xx_rawstac *archive);
/** Decoded length, or -1 before handle_base_info. */
XXFC_API int64_t xx_rawstac_get_unpacked_size(const xx_rawstac *archive);

static inline Abstractformat *xx_rawstac_to_format(xx_rawstac *archive) {
    return archive ? &archive->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_RAWSTAC_H */
