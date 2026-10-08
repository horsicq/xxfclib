/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_lif.h
 *  @brief The single-member "DC"/"DL" compressed-file container U3 calls LIF.
 */

/* WHAT THIS IS - AND WHAT IT IS NOT.  The corpus directory is named LIF and
 * U3's handler for it is labelled LIF, but this is NOT Hewlett-Packard's LIF
 * volume format: there is no 0x8000 volume header, no 256-byte directory and
 * no 8-character volume label anywhere in these files.  It is a small
 * single-member compressed-file wrapper whose members are the pieces of an
 * early-1990s Delrina WinFax installation (helphk.dll, windem.drv,
 * faxdem.exe, winfax.hlp, ...).  The name here follows U3's naming and the
 * corpus directory, nothing more.  It is unrelated to the LIF KD reader in
 * this library, whose members carry ASCII-hex headers.
 *
 * WHERE THE LAYOUT COMES FROM.  U3's recognition predicate (FUN_00555840,
 * reached from VMT slot 0 at 0x00555bf0) fixes six of the fields:
 *
 *   u16 @ 0x00 == 0x4344 ("DC") or 0x4c44 ("DL")
 *   u16 @ 0x02 == 2
 *   u16 @ 0x04 != 0
 *   i32 @ 0x15 >  0
 *   i32 @ 0x19 == 6
 *   i32 @ 0x1d >= 0
 *   i32 @ 0x21 == 0
 *
 * The remaining offsets were measured over all nine samples in
 * F:\ARC\ARC\LIF:
 *
 *   header, 41 bytes at offset 0, little endian:
 *     0x00  2    "DC" or "DL"
 *     0x02  u16  version, 2
 *     0x04  u16  member count, 1 in every sample
 *     0x06  15   member name, NUL padded ("helphk.dll", "faxdem.exe", ...)
 *     0x15  u32  TOTAL size of the container, header included
 *     0x19  u32  method, 6
 *     0x1d  u32  packed size
 *     0x21  u32  0
 *     0x25  u16  DOS date
 *     0x27  u16  DOS time
 *     0x29  ..   packed bytes
 *
 * TWO STRUCTURAL FACTS, both true in 9 of 9 samples and both enforced here,
 * which is what makes a two-byte magic usable:
 *     total size == the file size, and
 *     packed size == total size - 41.
 *
 * THE DATE/TIME ASSIGNMENT IS NOT A GUESS.  Under the assignment above every
 * sample yields a valid DOS date in 1991 (months 5..9, days 6..29) and a
 * valid time.  Under the opposite assignment the "date" field decodes to the
 * years 2018..2059 and produces month 0 in one sample and month 14 in
 * another, i.e. impossible dates.  Only one of the two readings is
 * self-consistent, so that is the one used.
 *
 * METHOD 6 is Zoo-style LZD: little-bit-first 9..13-bit LZW with clear code
 * 256 and EOF code 257.  The header omits the raw size, so the reader first
 * scans the stream to determine its exact decoded length, then uses the
 * shared Zoo LZD decoder for extraction.  The packed length may include one
 * extra zero alignment byte after EOF; the decoder sees only the bytes
 * through EOF.  Decoding all nine corpus members yielded the expected
 * lengths, including helphk.dll at 2304 bytes, and its bytes matched U3.
 */

#ifndef XXFCLIB_FORMAT_LIF_H
#define XXFCLIB_FORMAT_LIF_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Header size in bytes. */
#define XX_LIF_HEADER_SIZE 41U
/** Member name field width. */
#define XX_LIF_NAME_SIZE 15U
/** Only version ever seen. */
#define XX_LIF_VERSION 2U
/** Only method ever seen; Zoo LZD. */
#define XX_LIF_METHOD 6U

typedef struct xx_lif xx_lif;
typedef struct xx_lif xx_lif_t;
typedef struct xx_lif XLif;

struct xx_lif {
    Abstractformat format; /**< Base format structure (first member). */
    int64_t packed_offset; /**< Absolute offset of the packed stream. */
    int64_t packed_size;   /**< Packed length, bounded to the file. */
    uint16_t dos_date;
    uint16_t dos_time;
};

XXFC_API void xx_lif_init(xx_lif *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_lif *xx_lif_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_lif_destroy(xx_lif *archive);
XXFC_API void xx_lif_free(xx_lif *archive);

XXFC_API bool xx_lif_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lif_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_lif_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_lif_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);
XXFC_API xx_archive_record_state *xx_lif_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_lif_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_lif_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_lif_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_lif_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Absolute offset of the packed stream, or -1 before handle_base_info. */
XXFC_API int64_t xx_lif_get_packed_offset(const xx_lif *archive);
/** Packed length, or -1 before handle_base_info. */
XXFC_API int64_t xx_lif_get_packed_size(const xx_lif *archive);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lif_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lif_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lif_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_LIF_H */
