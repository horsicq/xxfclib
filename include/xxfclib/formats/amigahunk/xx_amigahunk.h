/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_amigahunk.h
 * @brief Amiga Hunk (AmigaDOS load file / linker unit) executable format.
 *
 * A Hunk file is a flat sequence of typed hunks. Every hunk starts with a
 * big-endian longword whose low 30 bits hold the hunk type; the top two bits
 * carry AmigaDOS memory attributes and are masked off. An executable begins
 * with HUNK_HEADER (0x3F3), a relocatable linker unit with HUNK_UNIT (0x3E7).
 *
 * References:
 *   http://amiga-dev.wikidot.com/file-format:hunk
 *   Formats/exec/xamigahunk.cpp (XAmigaHunk)
 */

#ifndef XXFCLIB_FORMAT_AMIGAHUNK_H
#define XXFCLIB_FORMAT_AMIGAHUNK_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations and type aliases */
typedef struct xx_amigahunk xx_amigahunk;
typedef struct xx_amigahunk xx_amigahunk_t;
typedef struct xx_amigahunk XAmigaHunk;

/**
 * File type value for Amiga Hunk.
 *
 * xxfc_defs.h is shared and gets wired serially, so the enumerator does not
 * exist yet. Replace this macro with XX_FILE_TYPE_AMIGAHUNK once the
 * enumerator is added (proposed value 153, the next free slot after
 * XX_FILE_TYPE_SEAARC = 152).
 */
#define XX_AMIGAHUNK_FILE_TYPE ((xx_file_type_t)153)

/** Base virtual address AmigaDOS load files are mapped at by this reader. */
#define XX_AMIGAHUNK_IMAGE_BASE UINT64_C(0)

/** Mask that strips the AmigaDOS memory-attribute bits from a hunk id. */
#define XX_AMIGAHUNK_ID_MASK UINT32_C(0x3FFFFFFF)

/* Hunk type identifiers (low 30 bits of the leading longword). */
#define XX_AMIGAHUNK_HUNK_UNIT         UINT32_C(0x03E7)
#define XX_AMIGAHUNK_HUNK_NAME         UINT32_C(0x03E8)
#define XX_AMIGAHUNK_HUNK_CODE         UINT32_C(0x03E9)
#define XX_AMIGAHUNK_HUNK_DATA         UINT32_C(0x03EA)
#define XX_AMIGAHUNK_HUNK_BSS          UINT32_C(0x03EB)
#define XX_AMIGAHUNK_HUNK_RELOC32      UINT32_C(0x03EC)
#define XX_AMIGAHUNK_HUNK_RELOC16      UINT32_C(0x03ED)
#define XX_AMIGAHUNK_HUNK_RELOC8       UINT32_C(0x03EE)
#define XX_AMIGAHUNK_HUNK_EXT          UINT32_C(0x03EF)
#define XX_AMIGAHUNK_HUNK_SYMBOL       UINT32_C(0x03F0)
#define XX_AMIGAHUNK_HUNK_DEBUG        UINT32_C(0x03F1)
#define XX_AMIGAHUNK_HUNK_END          UINT32_C(0x03F2)
#define XX_AMIGAHUNK_HUNK_HEADER       UINT32_C(0x03F3)
#define XX_AMIGAHUNK_HUNK_OVERLAY      UINT32_C(0x03F5)
#define XX_AMIGAHUNK_HUNK_BREAK        UINT32_C(0x03F6)
#define XX_AMIGAHUNK_HUNK_DREL32       UINT32_C(0x03F7)
#define XX_AMIGAHUNK_HUNK_DREL16       UINT32_C(0x03F8)
#define XX_AMIGAHUNK_HUNK_DREL8        UINT32_C(0x03F9)
#define XX_AMIGAHUNK_HUNK_LIB          UINT32_C(0x03FA)
#define XX_AMIGAHUNK_HUNK_INDEX        UINT32_C(0x03FB)
#define XX_AMIGAHUNK_HUNK_RELOC32SHORT UINT32_C(0x03FC)
#define XX_AMIGAHUNK_HUNK_RELRELOC32   UINT32_C(0x03FD)
#define XX_AMIGAHUNK_HUNK_ABSRELOC16   UINT32_C(0x03FE)
#define XX_AMIGAHUNK_HUNK_DREL32EXE    UINT32_C(0x03FF)
#define XX_AMIGAHUNK_HUNK_PPC_CODE     UINT32_C(0x04E9)
#define XX_AMIGAHUNK_HUNK_RELRELOC26   UINT32_C(0x04EC)

/** Upper bound on the number of hunks walked out of one file. */
#define XX_AMIGAHUNK_MAX_HUNKS UINT32_C(0x10000)

/** One entry of the hunk table, in file order. */
typedef struct xx_amigahunk_hunk {
    uint32_t id;      /**< Hunk type, memory-attribute bits masked off */
    uint32_t raw_id;  /**< Leading longword exactly as stored */
    int64_t  offset;  /**< Absolute device offset of the leading longword */
    int64_t  size;    /**< Total hunk size in bytes, leading longword included */
} xx_amigahunk_hunk;

/**
 * @brief Concrete Amiga Hunk format structure.
 * Inherits from Abstractformat by placing it as the first member.
 */
struct xx_amigahunk {
    Abstractformat format;     /**< Base format structure (first member) */
    uint32_t magic;            /**< Leading hunk id: HUNK_HEADER or HUNK_UNIT */
    uint32_t strings_size;     /**< Resident-library name list size, longwords */
    uint32_t table_size;       /**< HUNK_HEADER: total number of hunks */
    uint32_t first_hunk;       /**< HUNK_HEADER: first loaded hunk index */
    uint32_t last_hunk;        /**< HUNK_HEADER: last loaded hunk index */
    int64_t  size_table_offset;/**< Offset of the HUNK_HEADER size table (-1) */
    uint32_t size_table_count; /**< Number of entries in that size table */
    xx_amigahunk_hunk *hunks;  /**< Parsed hunk table (owned) */
    uint32_t hunk_count;       /**< Number of entries in hunks */
    uint32_t loadable_count;   /**< CODE/DATA/BSS/PPC_CODE hunk count */
    bool     is_object;        /**< True when the file starts with HUNK_UNIT */
    bool     has_ppc_code;     /**< True when a HUNK_PPC_CODE hunk is present */
    bool     has_reloc32;      /**< True when a HUNK_RELOC32 hunk is present */
};

/* --- Constructors & Lifecycle --- */
XXFC_API void xx_amigahunk_init(xx_amigahunk *amigahunk, xx_io_device *dev, int64_t base_address);
XXFC_API xx_amigahunk *xx_amigahunk_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_amigahunk_destroy(xx_amigahunk *amigahunk);
XXFC_API void xx_amigahunk_free(xx_amigahunk *amigahunk);

/* --- Format Implementation Callbacks --- */
XXFC_API bool xx_amigahunk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_amigahunk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_amigahunk_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_amigahunk_get_memory_map(Abstractformat *self,
                                          xx_memory_map_mode_t mode,
                                          xx_memory_map *output,
                                          xx_pd_struct *pd);

/* --- Amiga Hunk Getters & Properties --- */
XXFC_API uint32_t xx_amigahunk_get_magic(const xx_amigahunk *amigahunk);
XXFC_API uint32_t xx_amigahunk_get_strings_size(const xx_amigahunk *amigahunk);
XXFC_API uint32_t xx_amigahunk_get_table_size(const xx_amigahunk *amigahunk);
XXFC_API uint32_t xx_amigahunk_get_first_hunk(const xx_amigahunk *amigahunk);
XXFC_API uint32_t xx_amigahunk_get_last_hunk(const xx_amigahunk *amigahunk);
XXFC_API int64_t xx_amigahunk_get_size_table_offset(const xx_amigahunk *amigahunk);
XXFC_API uint32_t xx_amigahunk_get_size_table_count(const xx_amigahunk *amigahunk);
XXFC_API uint32_t xx_amigahunk_get_number_of_hunks(const xx_amigahunk *amigahunk);
XXFC_API const xx_amigahunk_hunk *xx_amigahunk_get_hunk(const xx_amigahunk *amigahunk, uint32_t index);
XXFC_API bool xx_amigahunk_is_hunk_present(const xx_amigahunk *amigahunk, uint32_t hunk_id);
XXFC_API bool xx_amigahunk_is_object(const xx_amigahunk *amigahunk);
XXFC_API bool xx_amigahunk_has_ppc_code(const xx_amigahunk *amigahunk);
XXFC_API const char *xx_amigahunk_hunk_id_to_string(uint32_t hunk_id);

/* Cast helpers */
static inline Abstractformat *xx_amigahunk_to_format(xx_amigahunk *amigahunk) {
    return amigahunk ? &amigahunk->format : NULL;
}

static inline const Abstractformat *xx_amigahunk_to_format_const(const xx_amigahunk *amigahunk) {
    return amigahunk ? &amigahunk->format : NULL;
}

/* User-facing aliases without xx_ prefix */
static inline void XAmigaHunk_init(xx_amigahunk *amigahunk, xx_io_device *dev, int64_t base_address) {
    xx_amigahunk_init(amigahunk, dev, base_address);
}

static inline xx_amigahunk *XAmigaHunk_create(xx_io_device *dev, int64_t base_address) {
    return xx_amigahunk_create(dev, base_address);
}

static inline void XAmigaHunk_free(xx_amigahunk *amigahunk) {
    xx_amigahunk_free(amigahunk);
}

static inline bool XAmigaHunk_check_is_valid(xx_amigahunk *amigahunk, xx_pd_struct *pd) {
    return amigahunk ? xx_amigahunk_check_is_valid(&amigahunk->format, pd) : false;
}

static inline bool XAmigaHunk_handle_base_info(xx_amigahunk *amigahunk, xx_pd_struct *pd) {
    return amigahunk ? xx_amigahunk_handle_base_info(&amigahunk->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_AMIGAHUNK_H */
