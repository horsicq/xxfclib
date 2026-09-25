/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_sbx_extractor.h
 *  @brief SBX self-extractor (SBSETUP installer, "SB1" record chain). */

#ifndef XXFCLIB_FORMAT_SFX_SBX_EXTRACTOR_H
#define XXFCLIB_FORMAT_SFX_SBX_EXTRACTOR_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An SBX self-extracting installer (the "SBSETUP" stub).
 *
 * The carrier is an ordinary Windows executable, PE32 or 16-bit NE.  The
 * container behind it has no archive header, no directory and no member
 * count: it is a bare chain of records that runs to the last byte of the
 * file, and landing exactly on that byte is the only thing that closes it.
 *
 * One record:
 *
 *   0x00  char[4]  "SB1\0"          record tag
 *   0x04  i32 LE   record size      the WHOLE record: 0x12 + name + packed
 *   0x08  u16 LE   DOS date         (year - 1980) << 9 | month << 5 | day
 *   0x0a  u16 LE   DOS time         hour << 11 | minute << 5 | second / 2
 *   0x0c  u8       DOS attributes
 *   0x0d  u8       name length      1..255
 *   0x0e  char[]   name             not NUL terminated
 *   +     i32 LE   unpacked size
 *   +4    packed bytes, record size - name length - 0x12 of them
 *
 * The date word comes BEFORE the time word, the reverse of MS-DOS directory
 * entries; read the other way round the dates still decode, just wrongly.
 *
 * Every member is a plain Yoshizaki LZHUF stream with no framing and no end
 * symbol, decoded by xx_lzhuf_decode_memory(): F = 60, THRESHOLD = 2, the
 * -lh1- position tables, a ring preset to 0x20.  A member with neither packed
 * nor unpacked bytes is an empty file.
 *
 * Locating the chain: in a PE carrier it starts exactly at the overlay (the
 * end of the furthest section's raw data).  NE carriers are searched for the
 * tag in their first MiB, and a candidate is accepted only when its chain
 * lands on the end of the file - the stubs carry a stray "SB1\0" of their own
 * that parses as one record and then fails.  A bare chain with no executable
 * in front of it is not claimed.
 */
typedef struct xx_sfx_sbx_extractor {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t chain_offset;    /**< First record, relative to base_address. */
    uint64_t unpacked_total; /**< Sum of the members' unpacked sizes. */
    uint32_t carrier;        /**< XX_SFX_SBX_EXTRACTOR_CARRIER_*. */
    void *table;             /**< Member table cached by handle_base_info. */
} xx_sfx_sbx_extractor;

typedef xx_sfx_sbx_extractor xx_sfx_sbx_extractor_t;

#define XX_SFX_SBX_EXTRACTOR_CARRIER_NONE 0U
#define XX_SFX_SBX_EXTRACTOR_CARRIER_PE 1U
#define XX_SFX_SBX_EXTRACTOR_CARRIER_NE 2U

/** The record tag, "SB1\0". */
#define XX_SFX_SBX_EXTRACTOR_TAG_SIZE 4
/** Fixed bytes of a record besides the name: the 0x0e-byte header and the
 *  unpacked-size dword.  The record size is this plus the name plus the
 *  packed bytes. */
#define XX_SFX_SBX_EXTRACTOR_OVERHEAD 0x12

/** Compression method values published in XX_META_ID_COMPRESSION_METHOD. */
#define XX_SFX_SBX_EXTRACTOR_METHOD_STORED 0U
#define XX_SFX_SBX_EXTRACTOR_METHOD_LZHUF 1U

XXFC_API void xx_sfx_sbx_extractor_init(xx_sfx_sbx_extractor *archive,
                                        xx_io_device *device,
                                        int64_t base_address);
XXFC_API xx_sfx_sbx_extractor *xx_sfx_sbx_extractor_create(
    xx_io_device *device, int64_t base_address);
XXFC_API void xx_sfx_sbx_extractor_destroy(xx_sfx_sbx_extractor *archive);
XXFC_API void xx_sfx_sbx_extractor_free(xx_sfx_sbx_extractor *archive);

XXFC_API bool xx_sfx_sbx_extractor_check_is_valid(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API bool xx_sfx_sbx_extractor_handle_base_info(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API int64_t xx_sfx_sbx_extractor_get_format_size(Abstractformat *self,
                                                      xx_pd_struct *pd);
XXFC_API uint64_t xx_sfx_sbx_extractor_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sfx_sbx_extractor_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sfx_sbx_extractor_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sfx_sbx_extractor_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sfx_sbx_extractor_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sfx_sbx_extractor_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SFX_SBX_EXTRACTOR_H */
