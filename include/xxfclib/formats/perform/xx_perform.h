/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_perform.h
 *  @brief Delrina PerFORM / FormFlow "compressed database" document.
 */

/* WHERE THE LAYOUT COMES FROM.  There is no published specification for this
 * format.  The container and the codec below were both recovered from the
 * original unpacker's own code (U3, decompiled: format "PerFORM", class
 * `oza`, VMT 0x005f0d28; recognition FUN_005f0da0, open/unpack FUN_005f0de0,
 * codec FUN_004c5260 / FUN_004c55f0 with its helpers FUN_004c5490 read-code,
 * FUN_004c52e0 get-bits and FUN_004c53d0 block-align) and then verified
 * against all 782 samples in F:\ARC\ARC\PerFORM.
 *
 *   off  size  what                                       evidence
 *   0    33    "PerFORM compressed database 1.00 "        identical, 782/782
 *   33   1     0x00 terminator of that string             identical, 782/782
 *   34   2     u16 LE, CRC-16/ARC of the DECODED document verified, 782/782
 *   36   ..    packed LZW code stream to end of file
 *
 * That is the whole header: 36 bytes, which is exactly what the original
 * reads (`read(handle, buffer, 0x24)`) before handing the rest of the file to
 * the decompressor as `total_size - current_position`.  There is no declared
 * plaintext length, no member table and no trailer.
 *
 * Two corrections to an earlier measurement-only reading of this format are
 * worth recording, because both were reasonable and both were wrong:
 *
 *   - the header is 36 bytes, not 37.  The byte at 36 is zero in every
 *     sample not because it is a reserved field but because it is the first
 *     byte of the code stream: the stream opens with a 9-bit CLEAR code
 *     (0x100) packed least-significant-bit-first, whose low eight bits are
 *     0x00 and whose ninth bit is the low bit of byte 37.  That is also the
 *     origin of the "bit 0 of the payload at offset 37 is always 1" note -
 *     it was the top bit of the CLEAR code, not a marker.
 *   - the stream is LZW, not LZSS.  A "9-bit literal" reading appears to
 *     work for the first few bytes of many samples because a fresh LZW table
 *     emits single characters for a while; it then fails, which is why no
 *     consistent match descriptor could be found.  There are no match
 *     descriptors.
 *
 * THE CODEC.  A GIF-shaped LZW widened to thirteen bits:
 *
 *   - codes are packed least-significant-bit-first and start at nine bits;
 *   - 0x100 is CLEAR, 0x101 is END, and the first assignable code is 0x102;
 *   - the width grows when the next free code would no longer fit, i.e. when
 *     `limit < next_free` with `limit` holding (1 << width) - 1, and it stops
 *     at thirteen bits, where the table caps at 8192 entries and simply stops
 *     growing.  There is no early change and no block alignment: the original
 *     drives one shared LZW engine with a ten-field descriptor, and PerFORM
 *     constructs it as (max_bits 13, filter 0, has_END 1, has_CLEAR 1,
 *     block_align 0, msb_first 0, early_change 0, ...);
 *   - the stream must open with CLEAR.  After any CLEAR the width drops back
 *     to nine and the table back to 0x102, and the code that follows must be
 *     a literal - the original would read uninitialised state otherwise, so
 *     this reader rejects instead;
 *   - END terminates.  Running out of input without seeing END is a
 *     truncated stream, not a successful decode.
 *
 * VERIFICATION.  Every sample carries a CRC-16/ARC of its decoded document in
 * the header, so success is checked against something the file itself
 * asserts rather than against a plausible-looking result: 782 of 782 decode
 * and 782 of 782 match their stored CRC.  Output was additionally compared
 * byte-for-byte with the original unpacker on a sample, and the decoded
 * documents are the expected uncompressed PerFORM databases - they begin
 * "PerFORM PRO FORM  2.02", "Per:FORM database 2.10", "PerForm  1.06" and so
 * on.  Plaintext sizes run 1066..181789 bytes; the worst expansion measured
 * is 6.75x.
 *
 * WHAT THIS READER PUBLISHES.  One archive record.  The file is one stream,
 * not a container, and it carries no member name - the original names the
 * output after the archive file itself - so the record name is synthesised
 * as "database".  The 22-byte banner of the decoded document is surfaced as
 * the record comment, since it is the only self-description the payload has.
 *
 * ONE SUB-SHAPE, DELIBERATELY NOT ACTED ON.  Six samples decode to a document
 * whose banner reads "PerFORM PRO ENCR  2.02": the *decoded* database is
 * password protected internally.  The packed stream itself is not encrypted -
 * it decodes and checksums like any other - so the record is not marked
 * encrypted, which would wrongly imply that unpacking needs a password.  The
 * banner says so instead.
 */

#ifndef XXFCLIB_FORMAT_PERFORM_H
#define XXFCLIB_FORMAT_PERFORM_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** "PerFORM compressed database 1.00 " plus its NUL terminator. */
#define XX_PERFORM_SIGNATURE "PerFORM compressed database 1.00 "
/** Bytes compared by the magic test, terminator included. */
#define XX_PERFORM_SIGNATURE_SIZE 34U
/** Signature plus the u16 checksum at 34.  The payload starts at 36. */
#define XX_PERFORM_HEADER_SIZE 36U

/** Widest code the dialect uses. */
#define XX_PERFORM_MAX_BITS 13U
/** Table capacity, 1 << XX_PERFORM_MAX_BITS.  Also the longest phrase a
 *  prefix chain can reach, so it sizes the reversal stack. */
#define XX_PERFORM_MAX_CODES 8192U
/** Reset the width and the table. */
#define XX_PERFORM_CLEAR_CODE 0x100U
/** End of stream. */
#define XX_PERFORM_END_CODE 0x101U
/** First assignable dictionary slot. */
#define XX_PERFORM_FIRST_CODE 0x102U

/** Largest packed stream this reader will buffer.  The corpus maximum is
 *  60569 bytes; this is three orders of magnitude of headroom. */
#define XX_PERFORM_MAX_PACKED_SIZE ((int64_t)64 * 1024 * 1024)
/** Ceiling on the decoded size, as a multiple of the packed size.  LZW has
 *  no tight structural ratio - a single thirteen-bit code can carry up to
 *  8192 bytes - so this is a policy bound, not a derived one: the measured
 *  worst case over the corpus is 6.75x and anything past 256x is treated as
 *  a broken stream rather than decoded to exhaustion. */
#define XX_PERFORM_MAX_EXPANSION 256
/** Absolute ceiling on the decoded size, applied together with the ratio. */
#define XX_PERFORM_MAX_UNPACKED_SIZE ((uint64_t)512 * 1024 * 1024)

/** Bytes of the decoded document kept as its self-description. */
#define XX_PERFORM_BANNER_SIZE 22U

typedef struct xx_perform xx_perform;
typedef struct xx_perform xx_perform_t;
typedef struct xx_perform XPerform;

struct xx_perform {
    Abstractformat format;
    uint16_t checksum;          /**< CRC-16/ARC of the decoded document, as
                                 *   stored at offset 34. */
    bool checksum_valid;        /**< True when the decode reproduced it. */
    int64_t packed_offset;      /**< Absolute device offset of the stream. */
    int64_t packed_size;        /**< Packed stream length, bounded to file. */
    uint64_t uncompressed_size; /**< Decoded length, 0 if it would not
                                 *   decode. */
    bool unpackable;            /**< Decoded cleanly and matched the CRC. */
    /** Banner of the decoded document, NUL terminated, empty when the stream
     *  did not decode or the bytes were not printable. */
    char banner[XX_PERFORM_BANNER_SIZE + 1U];
};

XXFC_API void xx_perform_init(xx_perform *document, xx_io_device *device,
                              int64_t base_address);
XXFC_API xx_perform *xx_perform_create(xx_io_device *device,
                                       int64_t base_address);
XXFC_API void xx_perform_destroy(xx_perform *document);
XXFC_API void xx_perform_free(xx_perform *document);

XXFC_API bool xx_perform_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_perform_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_perform_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_perform_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

/** Decode the packed stream to @p destination.  @p destination may be NULL,
 *  in which case the stream is only validated and measured.  Fails unless the
 *  decoded bytes reproduce the CRC-16/ARC stored in the header. */
XXFC_API bool xx_perform_unpack_to_device(xx_perform *document,
                                          xx_io_device *destination,
                                          xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_perform_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_perform_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_perform_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_perform_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_perform_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** CRC-16/ARC of the decoded document as stored in the header. */
XXFC_API uint16_t xx_perform_get_checksum(const xx_perform *document);
/** True when the decoded bytes reproduced the stored checksum. */
XXFC_API bool xx_perform_is_checksum_valid(const xx_perform *document);
/** Absolute offset of the packed stream, or -1 before handle_base_info. */
XXFC_API int64_t xx_perform_get_packed_offset(const xx_perform *document);
/** Packed stream length in bytes, or -1 before handle_base_info. */
XXFC_API int64_t xx_perform_get_packed_size(const xx_perform *document);
/** Decoded length in bytes, or 0 when the stream would not decode. */
XXFC_API uint64_t xx_perform_get_uncompressed_size(const xx_perform *document);
/** Banner of the decoded document, never NULL, empty when unknown. */
XXFC_API const char *xx_perform_get_banner(const xx_perform *document);

static inline Abstractformat *xx_perform_to_format(xx_perform *document) {
    return document ? &document->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PERFORM_H */
