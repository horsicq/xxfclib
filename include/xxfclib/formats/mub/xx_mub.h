/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_mub.h @brief Mach-O universal ("fat") binary reader. */

/* A universal binary is a table of architecture slices, each an ordinary
 * Mach-O image (or static library) stored whole at an aligned offset:
 *
 *   +0   u32  magic       CA FE BA BE  classic fat, big-endian fields
 *                         CA FE BA BF  fat64, big-endian, 64-bit offsets
 *                         B9 FA F1 0E  little-endian variant (7-Zip "Mub")
 *   +4   u32  nfat_arch   number of slices
 *   +8        fat_arch[nfat_arch]
 *
 *   fat_arch    (20 bytes): cputype, cpusubtype, offset, size, align
 *   fat_arch_64 (32 bytes): cputype, cpusubtype, offset(u64), size(u64),
 *                           align, reserved
 *
 * align is a power of two exponent. CA FE BA BE is also the Java class-file
 * magic; a class file stores minor/major version there, and every Java major
 * version is >= 45, so a fat header is told apart by bytes 4..6 being zero
 * and nfat_arch being small (1..XX_MUB_MAX_SLICES, well below 45).
 *
 * Each slice becomes one record, named after its CPU the way 7-Zip names
 * the extension ("x86", "x64", "arm64", "ppc", "cpu99_64-5", ...). A name
 * that repeats gets "_<index>" appended so no record overwrites another.
 */

#ifndef XXFCLIB_FORMAT_MUB_H
#define XXFCLIB_FORMAT_MUB_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Highest nfat_arch accepted; must stay below 45 (Java major versions). */
#define XX_MUB_MAX_SLICES 20U

typedef struct xx_mub xx_mub;
typedef struct xx_mub xx_mub_t;
typedef struct xx_mub XMub;

struct xx_mub {
    Abstractformat format;
    uint64_t number_of_records;
    uint32_t magic;      /**< First four bytes read big-endian. */
    bool is_fat64;       /**< CA FE BA BF: 32-byte fat_arch_64 entries. */
    int64_t archive_end; /**< End of the furthest slice, or -1. */
    void *internal;
};

XXFC_API void xx_mub_init(xx_mub *mub, xx_io_device *dev, int64_t base_address);
XXFC_API xx_mub *xx_mub_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_mub_destroy(xx_mub *mub);
XXFC_API void xx_mub_free(xx_mub *mub);

XXFC_API bool xx_mub_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_mub_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_mub_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API uint64_t xx_mub_get_number_of_archive_records(Abstractformat *self, xx_pd_struct *pd);

XXFC_API xx_archive_record_state *xx_mub_create_archive_records_reading(Abstractformat *self, const xx_list_s *options, xx_pd_struct *pd);
XXFC_API const xx_archive_record *xx_mub_get_current_archive_record(Abstractformat *self, xx_archive_record_state *state);
XXFC_API bool xx_mub_unpack_current_archive_record(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API bool xx_mub_archive_record_move_to_next(Abstractformat *self, xx_archive_record_state *state, xx_pd_struct *pd);
XXFC_API void xx_mub_free_archive_records_reading(Abstractformat *self, xx_archive_record_state *state);

XXFC_API uint64_t xx_mub_get_number_of_records(const xx_mub *mub);
XXFC_API int64_t xx_mub_get_archive_end(const xx_mub *mub);

static inline Abstractformat *xx_mub_to_format(xx_mub *mub)
{
    return mub ? &mub->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MUB_H */
