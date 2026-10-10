/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sis.h @brief Symbian / EPOC installation package (SIS, SISX) reader. */

#ifndef XXFCLIB_FORMAT_SIS_H
#define XXFCLIB_FORMAT_SIS_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Symbian installation package, in either of its two generations.
 *
 * EPOC SIS (Psion / Nokia, EPOC release 3 to 6), all little endian:
 *
 *   0x00  u32  UID1  package UID
 *   0x04  u32  UID2  0x1000006D (EPOC r3/4/5) or 0x10003A12 (EPOC r6)
 *   0x08  u32  UID3  0x10000419
 *   0x0C  u32  UID4  checksum of UID1..UID3
 *   0x10  u16  CRC-16 of the file
 *   0x12  u16  number of languages, 0x14 u16 number of file records
 *   0x16  u16  number of requisites ... 0x20 u32 installer version
 *   0x24  u16  options: 0x01 strings are UTF-16, 0x08 files not compressed
 *   0x30  u32  languages pointer, 0x34 u32 file records pointer,
 *   0x38  u32  requisites pointer, 0x3C certificates, 0x40 component name
 *
 *   A file record is {u32 record type, u32 file type, u32 details,
 *   u32 source name length, u32 pointer, u32 destination name length,
 *   u32 pointer, u32 length[n], u32 pointer[n], (r6: u32 original
 *   length[n], u32 MIME length, u32 MIME pointer)} with n = 1 for a simple
 *   file (record type 0) and n = number of languages for a language set
 *   (record type 1).  Record types 3/4 (if/else-if) carry a u32 length and
 *   an expression; 5/6 (else/endif) are the bare type word.  In EPOC r6
 *   files are zlib streams unless option 0x08 is set.
 *
 * SISX (Symbian OS 9 and later): a 16-byte UID header (UID1 0x10201A7A,
 * UID2, UID3 = package UID, UID4 checksum) followed by one SISContents
 * field.  Every field is {u32 type, length (u32; when bit 31 is set, a
 * second u32 supplies bits 31..62), data, zero padding to a multiple of
 * 4}; the elements of a SISArray omit their type.  SISContents holds the
 * optional checksums, a SISCompressed SISController (file descriptions:
 * target path, lengths, data unit file index) and SISData (data units of
 * SISCompressed file bodies, stored or zlib).  Embedded controllers and
 * conditional install blocks are walked; each controller's SISDataIndex
 * selects the data unit its files live in.
 *
 * Members are named after their install target with the drive removed
 * ("!:\sys\bin\a.exe" becomes "sys/bin/a.exe"); a file without a target is
 * named after the base name of its source (EPOC) or "file_<unit>_<index>"
 * (SISX).  Language-set forks get the language code prefixed to the base
 * name ("EN.x.rsc").  Colliding names get a "~N" suffix.
 */
typedef struct xx_sis {
    Abstractformat format;
    uint32_t variant; /**< XX_SIS_VARIANT_* */
    uint64_t number_of_records;
} xx_sis;

typedef xx_sis xx_sis_t;

#define XX_SIS_VARIANT_NONE 0U
#define XX_SIS_VARIANT_EPOC 1U  /**< EPOC r3/4/5 */
#define XX_SIS_VARIANT_EPOC6 2U /**< EPOC r6 */
#define XX_SIS_VARIANT_SISX 3U  /**< Symbian OS 9 */

XXFC_API void xx_sis_init(xx_sis *archive, xx_io_device *device, int64_t base_address);
XXFC_API xx_sis *xx_sis_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_sis_destroy(xx_sis *archive);
XXFC_API void xx_sis_free(xx_sis *archive);

XXFC_API bool xx_sis_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_sis_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_sis_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_sis_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_sis_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_sis_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_sis_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_sis_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_sis_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_SIS_H */
