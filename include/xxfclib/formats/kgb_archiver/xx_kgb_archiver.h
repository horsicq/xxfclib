/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_kgb_archiver.h @brief KGB Archiver 2 (.kgb) archive reader.
 *
 * Layout (little endian), as written by KGB Archiver 2 beta 1:
 *
 *   0x00  8    "KGB2" in UTF-16LE (4B 00 47 00 42 00 32 00)
 *   0x08  u8   encrypted flag (0 or 1)
 *   0x09  u8   algorithm, 0 (stored) .. 7 (strongest PAQ-family model)
 *   0x0A  u8   compression mode (-m), 0 .. 9
 *   0x0B  u32  number of files
 *   0x0F  per file, 572 bytes:
 *         u32  size of the following record, always 560
 *         560  a _wfinddata64_t: u32 attrib, u32 padding, i64 create,
 *              i64 access, i64 write (Unix seconds), i64 size,
 *              wchar_t name[260] (NUL terminated; the rest is not cleared)
 *         u64  sum of all bytes of the file
 *   then   the data of every file, concatenated in record order.  With
 *          algorithm 0 it is stored as is; otherwise it is one solid
 *          context-mixing stream.  An encrypted archive has an 8-byte value
 *          in front of the (block-encrypted) data.
 *
 * Records: every file in header order, with its size, attributes, write
 * time and algorithm.  Only algorithm 0 without encryption is extracted
 * (and checked against the byte sum); the PAQ-family algorithms 1..7 and
 * encrypted archives are listed but their unpack returns false.
 */

#ifndef XXFCLIB_FORMAT_KGB_ARCHIVER_H
#define XXFCLIB_FORMAT_KGB_ARCHIVER_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_kgb_archiver {
    Abstractformat format;
    uint64_t number_of_records;
    uint64_t total_unpacked_size;
    int64_t data_offset; /**< Absolute offset of the data section. */
    uint8_t algorithm;
    uint8_t mode;
    bool encrypted;
} xx_kgb_archiver;

typedef xx_kgb_archiver xx_kgb_archiver_t;

/** Size of the fixed archive header. */
#define XX_KGB_ARCHIVER_HEADER_SIZE 15
/** Size of one file entry (length field + _wfinddata64_t + byte sum). */
#define XX_KGB_ARCHIVER_ENTRY_SIZE 572
/** XX_META_ID_COMPRESSION_METHOD value of a stored member; 1..7 are the
 * PAQ-family algorithms of the same number. */
#define XX_KGB_ARCHIVER_METHOD_STORED 0

XXFC_API void xx_kgb_archiver_init(xx_kgb_archiver *archive,
                                   xx_io_device *device,
                                   int64_t base_address);
XXFC_API xx_kgb_archiver *xx_kgb_archiver_create(xx_io_device *device,
                                                 int64_t base_address);
XXFC_API void xx_kgb_archiver_destroy(xx_kgb_archiver *archive);
XXFC_API void xx_kgb_archiver_free(xx_kgb_archiver *archive);

XXFC_API bool xx_kgb_archiver_check_is_valid(Abstractformat *self,
                                             xx_pd_struct *pd);
XXFC_API bool xx_kgb_archiver_handle_base_info(Abstractformat *self,
                                               xx_pd_struct *pd);
XXFC_API int64_t xx_kgb_archiver_get_format_size(Abstractformat *self,
                                                 xx_pd_struct *pd);
XXFC_API uint64_t xx_kgb_archiver_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_kgb_archiver_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_kgb_archiver_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_kgb_archiver_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_kgb_archiver_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_kgb_archiver_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_KGB_ARCHIVER_H */
