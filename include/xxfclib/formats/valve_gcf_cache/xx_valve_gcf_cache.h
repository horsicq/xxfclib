/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/** @file xx_valve_gcf_cache.h
 *  @brief Valve Steam game cache file (.gcf), versions 1.3, 1.5 and 1.6.
 *
 * All fields are little-endian u32.  The file is a chain of tables:
 *
 *   header (11 u32): 1, 1, minor (3/5/6), cache id, last version played,
 *                    0, 0, file size, data block size, data block count,
 *                    header checksum
 *   block entry header (8 u32): count (= data block count), used, 5 x 0,
 *                    checksum
 *   block entries (count x 7 u32): flags, file data offset, file data size,
 *                    first data block, next block entry, previous block
 *                    entry, directory item
 *   fragmentation map header (4 u32): count, first unused, terminator,
 *                    checksum; then count x u32 "next data block"
 *   minor < 6 only: block entry map header (5 u32) + count x {prev, next}
 *   directory (header 14 u32; its directory_size covers header, item
 *                    entries (7 u32 each: name offset, size, checksum index,
 *                    flags, parent, next sibling, first child), names,
 *                    info1, info2, copy and local tables)
 *   minor >= 5: directory map header (2 u32);  then item count x u32
 *                    "first block entry" (the directory map)
 *   checksum header (2 u32: 1, size) + size bytes of checksums/signature
 *   data block header (minor >= 5: 6 u32 last version, block count, block
 *                    size, first block offset, used, checksum; minor 3
 *                    lacks the first field)
 *   data blocks at first block offset + index * block size.
 *
 * A file item (flag 0x4000) is stored as a chain of block entries (next
 * block entry, ending at an index >= count); each block entry covers
 * file_data_size bytes starting at file_data_offset, read from a chain of
 * data blocks linked through the fragmentation map.  The first block entry
 * of an item comes from the directory map (1.6) or, for older versions,
 * from walking the block entry map in order.
 *
 * Members are listed with their full path; folders are listed too.
 * Encrypted items (flag 0x100) and items whose data is not fully present
 * (partially downloaded caches) are listed but fail to unpack.  Unsafe
 * names (absolute, drive letters, "..", control characters, Windows device
 * names incl. CONIN$/CONOUT$/CLOCK$) are listed and refused on extraction;
 * names repeating an earlier one (case-insensitive) get a "__N" suffix.
 * NCF files (header 1, 2, 1) carry no data and are not handled.
 */

#ifndef XXFCLIB_FORMAT_VALVE_GCF_CACHE_H
#define XXFCLIB_FORMAT_VALVE_GCF_CACHE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_valve_gcf_cache {
    Abstractformat format;
    uint32_t minor_version;
    uint32_t block_size;
    uint32_t block_count;
    uint64_t number_of_records;
    uint64_t encrypted_members;
    uint64_t incomplete_members;
} xx_valve_gcf_cache;

typedef xx_valve_gcf_cache xx_valve_gcf_cache_t;

XXFC_API void xx_valve_gcf_cache_init(xx_valve_gcf_cache *archive,
                                      xx_io_device *device,
                                      int64_t base_address);
XXFC_API xx_valve_gcf_cache *xx_valve_gcf_cache_create(xx_io_device *device,
                                                       int64_t base_address);
XXFC_API void xx_valve_gcf_cache_destroy(xx_valve_gcf_cache *archive);
XXFC_API void xx_valve_gcf_cache_free(xx_valve_gcf_cache *archive);

XXFC_API bool xx_valve_gcf_cache_check_is_valid(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_valve_gcf_cache_handle_base_info(Abstractformat *self,
                                                  xx_pd_struct *pd);
XXFC_API int64_t xx_valve_gcf_cache_get_format_size(Abstractformat *self,
                                                    xx_pd_struct *pd);
XXFC_API uint64_t xx_valve_gcf_cache_get_number_of_archive_records(
    Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *
xx_valve_gcf_cache_create_archive_records_reading(Abstractformat *self,
                                                  const xx_list_s *options,
                                                  xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_valve_gcf_cache_get_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_valve_gcf_cache_unpack_current_archive_record(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_valve_gcf_cache_archive_record_move_to_next(
    Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_valve_gcf_cache_free_archive_records_reading(
    Abstractformat *self, xx_archive_record_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VALVE_GCF_CACHE_H */
