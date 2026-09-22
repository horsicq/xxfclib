/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_pdb.h @brief Palm OS database (.pdb / .prc) reader. */

#ifndef XXFCLIB_FORMAT_PDB_H
#define XXFCLIB_FORMAT_PDB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A Palm OS database container.
 *
 * PalmOS DatabaseHdrType, 78 bytes, every field BIG endian:
 *   0x00  char[32] database name, NUL terminated and NUL padded; 8-bit, so
 *         national character sets appear here and a 7-bit gate would be wrong
 *   0x20  u16 attributes; bit 0 = resource database (.prc).  Bits 0..11, 14
 *         and 15 are defined, so 0x7000 is always clear
 *   0x22  u16 version
 *   0x24  u32 creation date      0x28  u32 modification date
 *   0x2c  u32 last backup date   0x30  u32 modification number
 *   0x34  u32 appInfoID   file offset or 0
 *   0x38  u32 sortInfoID  file offset or 0
 *   0x3c  4CC type        0x40  4CC creator   (both registered ASCII)
 *   0x44  u32 uniqueIDSeed      0x48  u32 nextRecordListID
 *   0x4c  u16 number of entries
 *   0x4e  the entry list
 *
 * Record databases use 8-byte entries (u32 offset, u8 attributes,
 * u24 uniqueID); resource databases use 10-byte entries (4CC type, u16 id,
 * u32 offset).  Block sizes are NOT stored: every block runs to the start of
 * the next one and the last runs to EOF, which is what makes the monotonic
 * offset walk the format's real validator.
 *
 * Every record or resource, plus the optional appInfo and sortInfo blocks,
 * is exposed as a STORED member.  Payload codecs that individual Palm
 * applications layer inside their records - iSilo's "ToGoToGo" block codec
 * above all - are NOT decoded; those records are emitted verbatim.
 *
 * Ported from XArchive's packages/xpalmdatabase.cpp.
 */
typedef struct xx_pdb {
    Abstractformat format;
    uint64_t number_of_records;
    int64_t header_size; /**< 78 + the entry table. */
    bool is_resource_database;
} xx_pdb;

typedef xx_pdb xx_pdb_t;

XXFC_API void xx_pdb_init(xx_pdb *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_pdb *xx_pdb_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_pdb_destroy(xx_pdb *archive);
XXFC_API void xx_pdb_free(xx_pdb *archive);

XXFC_API bool xx_pdb_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_pdb_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_pdb_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API uint64_t xx_pdb_get_number_of_archive_records(Abstractformat *self,
                                                       xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_pdb_create_archive_records_reading(
    Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_pdb_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_pdb_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_pdb_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_pdb_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_PDB_H */
