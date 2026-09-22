/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_spk.h
 *  @brief !Spark / SparkFS archive (Acorn RISC OS), the ARC-style variant.
 */

/* WHAT THIS IS.  David Pilling's !Spark wrote the sequential SEA-ARC member
 * chain rather than ArcFS's central directory, and extended each member
 * header with the three RISC OS words.  The corpus files carry .zip and .arc
 * extensions but are neither.
 *
 * WHERE THE LAYOUT COMES FROM.  Two sources agree, and both were used.
 *
 * U3's recognition predicate (FUN_005b5860, reached from VMT slot 0 at
 * 0x005b6540) reads: byte 0 is 0x1a; byte 1 is a method in a bitmap indexed
 * by (method + 0x80) & 0xff; i32 at 0x0f (the packed size) is >= 0 and
 * smaller than the file; for method 0x82 the size at 0x19 must equal the one
 * at 0x0f, and when both are zero the byte at 0x29 must be 0x1a - which is
 * how it asserts the 41-byte header length; for any method above 0x81 the
 * size at 0x19 must be >= 0; the 13-byte name must be non-empty printable
 * ASCII; and a member whose every size and stamp is zero is rejected.
 *
 * deark's ARC/Spark module (vendored in XArchive as
 * Algos/xdearkmodule_arc_p.cpp, FMT_SPARK) supplies the rest: the header
 * grows by four bytes unless the masked method is 1, by twelve more when the
 * method's high bit is set, the method table, and the rule that identifies a
 * directory.
 *
 *   member header, 41 bytes for every Spark method:
 *     0x00  1    0x1a
 *     0x01  1    method; 0x00 or 0x80 is the end-of-archive marker
 *     0x02  13   name, NUL padded
 *     0x0f  u32  packed size
 *     0x13  u16  DOS date
 *     0x15  u16  DOS time
 *     0x17  u16  CRC-16
 *     0x19  u32  original size            (absent only for masked method 1)
 *     0x1d  u32  RISC OS load address     (Spark extension)
 *     0x21  u32  RISC OS exec address     (Spark extension)
 *     0x25  u32  RISC OS attributes       (Spark extension)
 *     0x29  ..   packed bytes
 *
 *   methods:  0x81 stored (old), 0x82 stored, 0x83 packed (RLE90),
 *             0x84 squeezed, 0x85/0x86/0x87 crunched 5/6/7,
 *             0x88 crunched (LZW then RLE90), 0x89 squashed (13-bit LZW),
 *             0xff compressed (LZW, no RLE90)
 *
 *   directory: method 0x82 AND the load address is file-typed
 *              ((load & 0xfff00000) == 0xfff00000) with type 0xddc.  Its
 *              "packed bytes" are a nested member chain, walked in place.
 *
 * CONFIRMED AGAINST THE CORPUS.  All six samples in F:\ARC\ARC\SPK walk end
 * to end with no byte left over, producing 36, 36, 37, 36, 36 and 44 files in
 * nine directories.  Only three methods occur - 0x82 (78 members), 0xff (164)
 * and 0x88 (36) - and all three have decoders here.  For b5052410.arc the
 * sum of the original sizes is 725307, which is exactly the figure the
 * reference unpacker reports for that archive.
 *
 * WHAT IS NOT IMPLEMENTED.  Methods 0x84 through 0x87 (squeezed and the three
 * older crunch variants) are recognised and listed but not decoded; unpacking
 * such a member fails rather than emitting a guess.  None occurs in this
 * corpus.  The stored CRC-16 is surfaced but not verified: it is ARC's own
 * variant and nothing here would be improved by a checksum this reader cannot
 * independently confirm.
 */

#ifndef XXFCLIB_FORMAT_SPK_H
#define XXFCLIB_FORMAT_SPK_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Member marker byte. */
#define XX_SPK_MARKER 0x1aU
/** Full Spark member header size. */
#define XX_SPK_HEADER_SIZE 41U
/** Name field width. */
#define XX_SPK_NAME_SIZE 13U
/** RISC OS file type that marks a directory member. */
#define XX_SPK_TYPE_DIRECTORY 0xddcU
/** Deepest directory nesting walked. */
#define XX_SPK_MAX_DEPTH 24U
/** Ceiling on the member count, so a malformed chain cannot loop forever. */
#define XX_SPK_MAX_MEMBERS 100000U

typedef struct xx_spk xx_spk;
typedef struct xx_spk xx_spk_t;
typedef struct xx_spk XSpk;

struct xx_spk {
    Abstractformat format;      /**< Base format structure (first member). */
    uint64_t number_of_records; /**< Files and directories together. */
    uint64_t number_of_files;   /**< Files only. */
};

XXFC_API void xx_spk_init(xx_spk *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_spk *xx_spk_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_spk_destroy(xx_spk *archive);
XXFC_API void xx_spk_free(xx_spk *archive);

XXFC_API bool xx_spk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_spk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_spk_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_spk_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_spk_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_spk_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_spk_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_spk_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_spk_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SPK_H */
