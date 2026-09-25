/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_kwaj.h @brief Microsoft KWAJ ("COMPRESS.EXE -Z") stream reader. */

#ifndef XXFCLIB_FORMAT_KWAJ_H
#define XXFCLIB_FORMAT_KWAJ_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The KWAJ variant of the MS-DOS installation compression family
 * (SZDD / SZ / KWAJ), as written by COMPRESS.EXE and read by EXPAND.EXE.
 *
 *   0x00  8   magic "KWAJ" 88 F0 27 D1
 *   0x08  u16 LE compression method
 *               0 stored, 1 XOR 0xFF, 2 LZSS (4 KiB window, spaces,
 *               write position 4096-18), 3 LZ + Huffman, 4 MSZIP
 *   0x0A  u16 LE offset of the compressed data (at least 14)
 *   0x0C  u16 LE header-extension flags; the fields present follow in
 *               this order:
 *         bit 0  u32 LE uncompressed length
 *         bit 1  u16 (meaning unknown)
 *         bit 2  u16 length + that many bytes
 *         bit 3  base name, NUL-terminated, at most 8 characters
 *         bit 4  extension, NUL-terminated, at most 3 characters
 *         bit 5  u16 length + that many bytes of text
 *   data_offset: the compressed stream, to the end of the file
 *
 * One member.  Its name is "<base>[.<ext>]" when the header carries one,
 * otherwise "payload".  Without the length field only the MSZIP chain has a
 * natural end; the other methods run to the end of the file.
 */
typedef struct xx_kwaj {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t method;
    uint32_t header_flags;
    int64_t data_offset;    /**< Relative to the format's base address. */
    bool has_length;        /**< The header stores the uncompressed length. */
    uint64_t unpacked_size; /**< Declared or measured; 0 when unknown. */
    bool unpacked_size_known;
} xx_kwaj;

typedef xx_kwaj xx_kwaj_t;

XXFC_API void xx_kwaj_init(xx_kwaj *archive, xx_io_device *device,
                           int64_t base_address);
XXFC_API xx_kwaj *xx_kwaj_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_kwaj_destroy(xx_kwaj *archive);
XXFC_API void xx_kwaj_free(xx_kwaj *archive);

XXFC_API bool xx_kwaj_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_kwaj_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_kwaj_get_format_size(Abstractformat *self,
                                         xx_pd_struct *pd);
XXFC_API uint64_t xx_kwaj_get_number_of_archive_records(Abstractformat *self,
                                                        xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_kwaj_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_kwaj_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_kwaj_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_kwaj_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_kwaj_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

/** Decode the member to @p destination (the header must parse). */
XXFC_API bool xx_kwaj_unpack_to_device(xx_kwaj *archive,
                                       xx_io_device *destination,
                                       xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_KWAJ_H */
