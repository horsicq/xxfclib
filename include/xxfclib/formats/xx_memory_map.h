/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_memory_map.h
 * @brief Common file-offset, virtual-address, and relative-address mappings.
 */

#ifndef XX_MEMORY_MAP_H
#define XX_MEMORY_MAP_H

#include "xxfclib/xxfc_defs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Invalid virtual/relative address. File offsets use -1 as their sentinel. */
#define XX_INVALID_ADDRESS UINT64_MAX
#define XX_MEMORY_MAP_INVALID_ADDRESS XX_INVALID_ADDRESS

/** Format-selectable views of a file. UNKNOWN asks for the format default. */
typedef enum xx_memory_map_mode_e {
    XX_MEMORY_MAP_MODE_UNKNOWN = 0,
    XX_MEMORY_MAP_MODE_REGIONS,
    XX_MEMORY_MAP_MODE_SEGMENTS,
    XX_MEMORY_MAP_MODE_SECTIONS,
    XX_MEMORY_MAP_MODE_OBJECTS,
    XX_MEMORY_MAP_MODE_STREAMS,
    XX_MEMORY_MAP_MODE_MAPS,
    XX_MEMORY_MAP_MODE_DATA
} xx_memory_map_mode_t;

typedef xx_memory_map_mode_t xx_map_mode_t;

/** Semantic role of a memory-map record. Values are combinable bit flags. */
typedef uint32_t xx_file_part_t;

#define XX_FILE_PART_UNKNOWN   UINT32_C(0)
#define XX_FILE_PART_REGION    (UINT32_C(1) << 0)
#define XX_FILE_PART_SECTION   (UINT32_C(1) << 1)
#define XX_FILE_PART_SEGMENT   (UINT32_C(1) << 2)
#define XX_FILE_PART_HEADER    (UINT32_C(1) << 4)
#define XX_FILE_PART_OVERLAY   (UINT32_C(1) << 5)
#define XX_FILE_PART_RESOURCE  (UINT32_C(1) << 6)
#define XX_FILE_PART_DEBUG     (UINT32_C(1) << 7)
#define XX_FILE_PART_DEBUGDATA XX_FILE_PART_DEBUG
#define XX_FILE_PART_STREAM    (UINT32_C(1) << 8)
#define XX_FILE_PART_SIGNATURE (UINT32_C(1) << 9)
#define XX_FILE_PART_FOOTER    (UINT32_C(1) << 10)
#define XX_FILE_PART_DATA      (UINT32_C(1) << 12)
#define XX_FILE_PART_OBJECT    (UINT32_C(1) << 13)
#define XX_FILE_PART_TABLE     (UINT32_C(1) << 14)
#define XX_FILE_PART_VALUE     (UINT32_C(1) << 15)
#define XX_FILE_PART_ALL       UINT32_MAX

#define XX_MEMORY_RECORD_NAME_SIZE 128U

/**
 * One half-open mapping interval. A physical record has offset >= 0. A
 * virtual-only record has offset == -1 and is_virtual == true. An unmapped
 * physical record (for example an overlay) has address == XX_INVALID_ADDRESS.
 */
typedef struct xx_memory_record_s {
    int64_t offset;
    uint64_t address;
    int64_t size;
    xx_file_part_t file_part;
    int32_t file_part_number;
    char name[XX_MEMORY_RECORD_NAME_SIZE];
    int32_t index;
    bool is_virtual;
    bool is_invisible;
} xx_memory_record;

typedef xx_memory_record xx_memory_record_t;
typedef xx_memory_record XX_MEMORY_RECORD;

/**
 * Common mapping data. Records are owned by the map and released by
 * xx_memory_map_cleanup(). binary_offset is an xxfclib extension which locates
 * an embedded format inside its I/O device; module_address is the image VA.
 */
typedef struct xx_memory_map_s {
    int64_t binary_offset;
    uint64_t module_address;
    bool is_image;
    int64_t image_size;
    int64_t binary_size;
    uint64_t entry_point_address;
    int64_t code_base;
    int64_t start_load_offset;
    xx_file_type_t file_type;
    xx_format_type_t format_type;
    xx_endian_t endian;
    xx_arch_t arch;
    xx_memory_map_mode_t mode;
    xx_memory_record *records;
    size_t record_count;
    size_t record_capacity;
} xx_memory_map;

typedef xx_memory_map xx_memory_map_t;
typedef xx_memory_map XX_MEMORY_MAP;

XXFC_API void xx_memory_map_init(xx_memory_map *map);
XXFC_API void xx_memory_map_cleanup(xx_memory_map *map);
XXFC_API bool xx_memory_map_reserve(xx_memory_map *map, size_t capacity);
XXFC_API bool xx_memory_map_add_record(xx_memory_map *map,
                                       const xx_memory_record *record);

/**
 * Add a format part. The physical portion is emitted first; when
 * virtual_size is larger than file_size, a virtual-only tail is emitted.
 */
XXFC_API bool xx_memory_map_add_part(xx_memory_map *map, int64_t offset,
                                     int64_t file_size, uint64_t address,
                                     int64_t virtual_size,
                                     xx_file_part_t file_part,
                                     int32_t file_part_number,
                                     const char *name, bool is_invisible);

/** Reindex records, validate binary bounds, and derive the image span. */
XXFC_API bool xx_memory_map_finalize(xx_memory_map *map);

XXFC_API uint64_t xx_memory_map_offset_to_address(const xx_memory_map *map,
                                                   int64_t offset);
XXFC_API int64_t xx_memory_map_address_to_offset(const xx_memory_map *map,
                                                 uint64_t address);

/**
 * Which record answers a lookup when more than one covers the same address
 * or offset, and what an unmapped or virtual-only record means to it.
 *
 * A well-formed image has no overlap and the two modes agree. They part on
 * the malformed ones -- overlapping section tables, a virtual size that
 * swallows the next section -- which is exactly the input a format
 * identifier spends its time on, so the choice is the caller's.
 */
typedef enum xx_memory_map_lookup_e {
    /**
     * Scan from the last record back, skipping virtual-only ones. The later
     * record wins, which suits a map built by refining earlier entries.
     */
    XX_MEMORY_MAP_LOOKUP_LAST_PHYSICAL = 0,
    /**
     * Scan from the first record on, and let the first record that covers
     * the address decide even when its answer is "not mapped": landing in a
     * section's virtual tail, or in an unmapped part such as an overlay,
     * ends the lookup rather than falling through to a later record that
     * happens to cover the same bytes.
     *
     * This is what the DIE signature engine has always done, and the
     * signature databases were written against it.
     */
    XX_MEMORY_MAP_LOOKUP_FIRST_MATCH
} xx_memory_map_lookup_t;

XXFC_API uint64_t xx_memory_map_offset_to_address_ex(
    const xx_memory_map *map, int64_t offset, xx_memory_map_lookup_t lookup);
XXFC_API int64_t xx_memory_map_address_to_offset_ex(
    const xx_memory_map *map, uint64_t address,
    xx_memory_map_lookup_t lookup);

XXFC_API uint64_t xx_memory_map_offset_to_relative_address(
    const xx_memory_map *map, int64_t offset);
XXFC_API int64_t xx_memory_map_relative_address_to_offset(
    const xx_memory_map *map, int64_t relative_address);
XXFC_API uint64_t xx_memory_map_relative_address_to_address(
    const xx_memory_map *map, int64_t relative_address);
XXFC_API int64_t xx_memory_map_address_to_relative_address(
    const xx_memory_map *map, uint64_t address);

XXFC_API bool xx_memory_map_is_offset_valid(const xx_memory_map *map,
                                             int64_t offset);
XXFC_API bool xx_memory_map_is_offset_range_valid(const xx_memory_map *map,
                                                   int64_t offset,
                                                   int64_t size);
XXFC_API bool xx_memory_map_is_address_valid(const xx_memory_map *map,
                                              uint64_t address);
XXFC_API bool xx_memory_map_is_address_range_valid(const xx_memory_map *map,
                                                    uint64_t address,
                                                    int64_t size);
XXFC_API bool xx_memory_map_is_relative_address_valid(
    const xx_memory_map *map, int64_t relative_address);
XXFC_API bool xx_memory_map_is_address_physical(const xx_memory_map *map,
                                                 uint64_t address);
XXFC_API bool xx_memory_map_is_relative_address_physical(
    const xx_memory_map *map, int64_t relative_address);
XXFC_API bool xx_memory_map_is_solid_address_range(const xx_memory_map *map,
                                                    uint64_t address,
                                                    int64_t size);
XXFC_API bool xx_memory_map_is_physical_address_range(
    const xx_memory_map *map, uint64_t address, int64_t size);

XXFC_API const xx_memory_record *xx_memory_map_record_by_offset(
    const xx_memory_map *map, int64_t offset);
XXFC_API const xx_memory_record *xx_memory_map_record_by_address(
    const xx_memory_map *map, uint64_t address);
XXFC_API const xx_memory_record *xx_memory_map_record_by_relative_address(
    const xx_memory_map *map, int64_t relative_address);
XXFC_API const xx_memory_record *xx_memory_map_record_by_index(
    const xx_memory_map *map, int32_t index);

/**
 * The @p index'th record that has file bytes, counting virtual-only records
 * as if they were not there.
 *
 * A part whose virtual size exceeds its file size occupies two records, and
 * the second is an artefact of that split rather than a part of the format.
 * A caller numbering the format's own parts -- "the third section" -- wants
 * this rather than a raw array index.
 *
 * @return NULL when there is no such record.
 */
XXFC_API const xx_memory_record *xx_memory_map_physical_record(
    const xx_memory_map *map, int32_t index);

/* Compatibility spellings matching XBinary's "RelAddress" terminology. */
static inline uint64_t xx_memory_map_offset_to_rel_address(
    const xx_memory_map *map, int64_t offset) {
    return xx_memory_map_offset_to_relative_address(map, offset);
}

static inline int64_t xx_memory_map_rel_address_to_offset(
    const xx_memory_map *map, int64_t relative_address) {
    return xx_memory_map_relative_address_to_offset(map, relative_address);
}

static inline uint64_t xx_memory_map_rel_address_to_address(
    const xx_memory_map *map, int64_t relative_address) {
    return xx_memory_map_relative_address_to_address(map, relative_address);
}

static inline int64_t xx_memory_map_address_to_rel_address(
    const xx_memory_map *map, uint64_t address) {
    return xx_memory_map_address_to_relative_address(map, address);
}

/* Short RVA spellings used by executable-format code. */
static inline uint64_t xx_memory_map_offset_to_rva(const xx_memory_map *map,
                                                    int64_t offset) {
    return xx_memory_map_offset_to_relative_address(map, offset);
}

static inline int64_t xx_memory_map_rva_to_offset(const xx_memory_map *map,
                                                   int64_t rva) {
    return xx_memory_map_relative_address_to_offset(map, rva);
}

#ifdef __cplusplus
}
#endif

#endif /* XX_MEMORY_MAP_H */
