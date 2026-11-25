/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_bflt.h @brief uClinux binary flat (bFLT revision 4) reader. */

#ifndef XXFCLIB_FORMAT_BFLT_H
#define XXFCLIB_FORMAT_BFLT_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XX_BFLT_HEADER_SIZE UINT32_C(64)
#define XX_BFLT_VERSION UINT32_C(4)

#define XX_BFLT_FLAG_RAM UINT32_C(0x01)
#define XX_BFLT_FLAG_GOTPIC UINT32_C(0x02)
#define XX_BFLT_FLAG_GZIP UINT32_C(0x04)
#define XX_BFLT_FLAG_GZDATA UINT32_C(0x08)
#define XX_BFLT_FLAG_KTRACE UINT32_C(0x10)

#define XX_BFLT_CPU_M68K UINT32_C(0x01)
#define XX_BFLT_CPU_386 UINT32_C(0x02)
#define XX_BFLT_CPU_ARM UINT32_C(0x04)
#define XX_BFLT_CPU_MIPS UINT32_C(0x08)
#define XX_BFLT_CPU_PPC UINT32_C(0x10)
#define XX_BFLT_CPU_SH UINT32_C(0x20)
#define XX_BFLT_CPU_COLDFIRE UINT32_C(0x40)

typedef struct xx_bflt_header {
    uint32_t revision;
    uint32_t entry;
    uint32_t data_start;
    uint32_t data_end;
    uint32_t bss_end;
    uint32_t stack_size;
    uint32_t relocation_start;
    uint32_t relocation_count;
    uint32_t flags;
    uint32_t build_date;
    uint32_t filler[5];
    uint32_t cpu_type;
} xx_bflt_header;

/** A relocation expressed in offsets relative to the start of the bFLT file. */
typedef struct xx_bflt_relocation {
    uint32_t patch_offset;
    uint32_t target_offset;
    bool from_got;
} xx_bflt_relocation;

typedef struct xx_bflt {
    Abstractformat format;
    xx_bflt_header header;
    xx_bflt_relocation *relocations;
    uint32_t number_of_relocations;
    uint32_t number_of_got_relocations;
    bool relocations_complete;
} xx_bflt;

XXFC_API void xx_bflt_init(xx_bflt *reader, xx_io_device *device, int64_t base_address);
XXFC_API xx_bflt *xx_bflt_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_bflt_destroy(xx_bflt *reader);
XXFC_API void xx_bflt_free(xx_bflt *reader);

XXFC_API bool xx_bflt_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bflt_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_bflt_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_bflt_get_memory_map(Abstractformat *self, xx_memory_map_mode_t mode, xx_memory_map *output, xx_pd_struct *pd);

XXFC_API const xx_bflt_header *xx_bflt_get_header(xx_bflt *reader, xx_pd_struct *pd);
XXFC_API uint32_t xx_bflt_get_number_of_relocations(xx_bflt *reader, xx_pd_struct *pd);
XXFC_API uint32_t xx_bflt_get_number_of_got_relocations(xx_bflt *reader, xx_pd_struct *pd);
XXFC_API const xx_bflt_relocation *xx_bflt_get_relocation(xx_bflt *reader, uint32_t index, xx_pd_struct *pd);
XXFC_API bool xx_bflt_are_relocations_complete(xx_bflt *reader, xx_pd_struct *pd);

static inline Abstractformat *xx_bflt_to_format(xx_bflt *reader)
{
    return reader ? &reader->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_BFLT_H */
