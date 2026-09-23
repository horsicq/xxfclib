/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lingvoarc.h @brief ABBYY Lingvo archive reader. */

#ifndef XXFCLIB_FORMAT_LINGVOARC_H
#define XXFCLIB_FORMAT_LINGVOARC_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The two ABBYY Lingvo / FineReader distribution-disk formats.
 *
 * Container ("lingvoArc1" / "lingvoArc2"), a multi-member volume:
 *
 *   file header, 18 bytes at 0:
 *     0x00  char[9] "lingvoArc"
 *     0x09  char    version, '1' or '2'
 *     0x0a  6 bytes 00 fd 00 df 00 ff, constant
 *     0x10  u16 LE  member count, never zero
 *
 *   index at 0x12, one fixed-stride entry per member.  Version 1 uses a
 *   54-byte entry, version 2 a 109-byte one:
 *     0x00           char[46] / char[101]  member path, backslash separated,
 *                    NUL terminated; bytes after the terminator are stale
 *                    producer-buffer content and are ignored
 *     0x2e / 0x65    u32 LE absolute offset of the member's descriptor
 *     0x32 / 0x69    u32 LE timestamp (Unix time)
 *
 *   descriptor, at that offset.  On every real volume the first one starts
 *   right after the index and each next one exactly where the previous
 *   payload ends; the reader requires the descriptors to follow the index in
 *   index order without overlapping (gaps are tolerated), so no two entries
 *   can share a payload.  Version 1 is 23 bytes, version 2 is 71:
 *     0x00           char[13] / char[61]  base name
 *     0x0d / 0x3d    u32 LE payload size
 *     0x11 / 0x41    u32 LE timestamp
 *     0x15 / 0x45    u16 LE method
 *   the payload follows the descriptor inline, `payload size` bytes.
 *
 *   Payloads are FINEAR streams (17-byte header, LHA -lh1- body, stored
 *   plaintext length and CRC-16/ARC), decoded and verified here.  The method
 *   word says whether a payload is whole: 0 is a self-contained member, 6 is
 *   the first fragment of a member that continues on the next volume (FINEAR
 *   header present, body cut off at the end of the volume) and 2 is the
 *   continuation fragment on the following volume.  A fragment cannot be
 *   decoded from one volume, so unpack fails closed for any method but 0.
 *   The version 2 field widths are U3's (FUN_0051cdc0); no version 2 sample
 *   has been seen.
 *
 * Stream ("LingvoArch"), one compressed file with no stored name or size:
 *
 *     0x00  char[10] "LingvoArch"
 *     0x0a  16 bytes, constant on every known file:
 *           u16 1 (version), u16 8176 (window), u16 256 (literals),
 *           u16 64 (match lengths), u16 327, u16 327, u16 0x831f,
 *           u16 321 (Huffman symbols)
 *     0x1a  u8[321] canonical Huffman code lengths, all non-zero, forming a
 *           complete code
 *     0x15b MSB-first bit stream of symbols: 0..255 literal, 256 end of
 *           stream, 259..320 a match of (symbol - 256) bytes followed by a
 *           13-bit absolute position in an 8176-byte zero-filled ring whose
 *           write cursor starts at 8174.  257 and 258 are never produced and
 *           are refused.  The stream is zero padded to a byte boundary.
 *
 *   The format has no checksum, so a damaged stream can only be refused when
 *   it breaks the code (bad position, missing end symbol).  check_is_valid
 *   checks the header, the code and the first 4096 bytes of the bit stream;
 *   handle_base_info walks the whole stream once to find its end and decoded
 *   length (capped at 512 MiB) and caches both.
 *
 * Member names are refused at unpack when they are absolute, contain ':',
 * control characters or <>"|?*, or have a component that is empty, ends in
 * '.' or ' ' (this covers "." and ".."), or is a Windows device name (CON,
 * PRN, AUX, NUL, COM0-9, LPT0-9, CLOCK$, CONIN$, CONOUT$, with or without
 * an extension).
 */
typedef enum xx_lingvoarc_variant_e {
    XX_LINGVOARC_VARIANT_NONE = 0,
    XX_LINGVOARC_VARIANT_CONTAINER = 1, /**< "lingvoArc1" / "lingvoArc2" */
    XX_LINGVOARC_VARIANT_STREAM = 2     /**< "LingvoArch" compressed file */
} xx_lingvoarc_variant;

typedef struct xx_lingvoarc {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t container_version; /**< 1 or 2 (container), 1 (stream). */
    uint32_t variant;           /**< xx_lingvoarc_variant. */
    uint64_t uncompressed_size; /**< Stream variant: decoded length. */
} xx_lingvoarc;

typedef xx_lingvoarc xx_lingvoarc_t;

XXFC_API void xx_lingvoarc_init(xx_lingvoarc *archive, xx_io_device *device,
                                int64_t base_address);
XXFC_API xx_lingvoarc *xx_lingvoarc_create(xx_io_device *device,
                                           int64_t base_address);
XXFC_API void xx_lingvoarc_destroy(xx_lingvoarc *archive);
XXFC_API void xx_lingvoarc_free(xx_lingvoarc *archive);

XXFC_API bool xx_lingvoarc_check_is_valid(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API bool xx_lingvoarc_handle_base_info(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API int64_t xx_lingvoarc_get_format_size(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_lingvoarc_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_lingvoarc_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lingvoarc_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lingvoarc_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lingvoarc_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lingvoarc_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LINGVOARC_H */
