/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_gns1.h @brief Apple C4000 GNS1 baseband firmware reader. */
#ifndef XXFCLIB_FORMAT_GNS1_H
#define XXFCLIB_FORMAT_GNS1_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_GNS1_SEGMENT_ENTRY_SIZE UINT32_C(12)
#define XX_GNS1_MIN_FILE_SIZE UINT32_C(64)
#define XX_GNS1_MIN_DATA_OFFSET UINT32_C(0x64)
#define XX_GNS1_MAX_SEGMENTS UINT32_C(1000)
#define XX_GNS1_FTAB_HEADER_SIZE UINT32_C(0x30)
#define XX_GNS1_FTAB_ENTRY_OFFSET UINT32_C(0x30)
#define XX_GNS1_FTAB_ENTRY_SIZE UINT32_C(16)
#define XX_GNS1_FTAB_MAX_ENTRIES UINT32_C(4096)
#define XX_GNS1_INTERNAL_BASE UINT32_C(0x10000000)

#define XX_GNS1_PERMISSION_EXECUTE UINT32_C(1)
#define XX_GNS1_PERMISSION_WRITE UINT32_C(2)
#define XX_GNS1_PERMISSION_READ UINT32_C(4)

typedef enum xx_gns1_segment_type_e {
    XX_GNS1_SEGMENT_TEXT = 0,
    XX_GNS1_SEGMENT_DATA = 1
} xx_gns1_segment_type;

typedef enum xx_gns1_region_e {
    XX_GNS1_REGION_UNKNOWN = 0,
    XX_GNS1_REGION_A = 1,
    XX_GNS1_REGION_B = 2
} xx_gns1_region;

/** One validated GNS1 load segment. Offsets are absolute device offsets. */
typedef struct xx_gns1_segment_s {
    uint32_t size;
    uint32_t physical_address;
    uint32_t image_offset;
    int64_t offset;
    uint64_t address;
    uint32_t permissions;
    xx_gns1_segment_type type;
    xx_gns1_region region;
} xx_gns1_segment;

/** One validated FTAB directory entry. The four-byte tag is NUL terminated. */
typedef struct xx_gns1_ftab_entry_s {
    char tag[5];
    uint32_t image_offset;
    uint32_t size;
    uint32_t reserved;
} xx_gns1_ftab_entry;

/** Program entry selected from the lowest text segment in one address region. */
typedef struct xx_gns1_entry_s {
    int64_t offset;
    uint64_t address;
    xx_gns1_region region;
} xx_gns1_entry;

typedef struct xx_gns1_s {
    Abstractformat format;
    uint64_t base_offset;
    uint64_t image_size;
    uint64_t table_size;
    xx_gns1_segment *segments;
    uint32_t segment_count;
    xx_gns1_ftab_entry *ftab_entries;
    uint32_t ftab_entry_count;
    xx_gns1_entry entries[2];
    uint32_t entry_count;
    bool is_ftab;
} xx_gns1;

typedef xx_gns1 xx_gns1_t;

XXFC_API void xx_gns1_init(xx_gns1 *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_gns1 *xx_gns1_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_gns1_destroy(xx_gns1 *reader);
XXFC_API void xx_gns1_free(xx_gns1 *reader);

XXFC_API bool xx_gns1_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_gns1_handle_base_info(Abstractformat *format, xx_pd_struct *pd);
XXFC_API int64_t xx_gns1_get_format_size(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_gns1_get_memory_map(Abstractformat *format, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API uint64_t xx_gns1_get_base_offset(const xx_gns1 *reader);
XXFC_API uint64_t xx_gns1_get_image_size(const xx_gns1 *reader);
XXFC_API uint64_t xx_gns1_get_table_size(const xx_gns1 *reader);
XXFC_API bool xx_gns1_is_ftab(const xx_gns1 *reader);
XXFC_API uint32_t xx_gns1_get_segment_count(const xx_gns1 *reader);
XXFC_API const xx_gns1_segment *xx_gns1_get_segment(const xx_gns1 *reader, uint32_t index);
XXFC_API uint32_t xx_gns1_get_ftab_entry_count(const xx_gns1 *reader);
XXFC_API const xx_gns1_ftab_entry *xx_gns1_get_ftab_entry(const xx_gns1 *reader, uint32_t index);
XXFC_API uint32_t xx_gns1_get_entry_count(const xx_gns1 *reader);
XXFC_API const xx_gns1_entry *xx_gns1_get_entry(const xx_gns1 *reader, uint32_t index);

/** Metadata strings used by Radare2's GNS1 reader. */
XXFC_API const char *xx_gns1_get_cpu_name(void);
XXFC_API const char *xx_gns1_get_machine_name(void);
XXFC_API const char *xx_gns1_get_subsystem_name(void);

static inline Abstractformat *xx_gns1_to_format(xx_gns1 *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_GNS1_H */
