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
 * @file xx_atarist.h
 * @brief Atari ST GEMDOS executable (PRG/TOS/TTP) format implementation.
 *
 * The GEMDOS program header is 28 bytes, big-endian throughout:
 *
 *   0x00 uint16 magic        0x601A
 *   0x02 uint32 text size
 *   0x06 uint32 data size
 *   0x0A uint32 bss size
 *   0x0E uint32 symbol table size
 *   0x12 uint32 reserved     (0)
 *   0x16 uint32 prgflags
 *   0x1A uint16 absflag      non-zero => no relocation table follows
 */

#ifndef XXFCLIB_FORMAT_ATARIST_H
#define XXFCLIB_FORMAT_ATARIST_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations and type aliases */
typedef struct xx_atarist xx_atarist;
typedef struct xx_atarist xx_atarist_t;
typedef struct xx_atarist XAtariST;

/**
 * @brief Concrete Atari ST GEMDOS executable format structure.
 * Inherits from Abstractformat by placing it as the first member.
 */
struct xx_atarist {
    Abstractformat format;       /**< Base format structure (first member) */
    uint16_t       magic;        /**< GEMDOS magic (0x601A) */
    uint32_t       text_size;    /**< Size of the TEXT segment */
    uint32_t       data_size;    /**< Size of the DATA segment */
    uint32_t       bss_size;     /**< Size of the BSS segment (not in file) */
    uint32_t       symbol_size;  /**< Size of the symbol table */
    uint32_t       reserved;     /**< Reserved, zero on well-formed files */
    uint32_t       flags;        /**< PRGFLAGS */
    uint16_t       relocation;   /**< ABSFLAG: non-zero => no relocation table */
};

/* --- Constructors & Lifecycle --- */
XXFC_API void xx_atarist_init(xx_atarist *atarist, xx_io_device *dev, int64_t base_address);
XXFC_API xx_atarist *xx_atarist_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_atarist_free(xx_atarist *atarist);
XXFC_API void xx_atarist_destroy(xx_atarist *atarist);

/* --- Format Implementation Callbacks --- */
XXFC_API bool xx_atarist_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_atarist_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_atarist_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_atarist_get_memory_map(Abstractformat *self,
                                        xx_memory_map_mode_t mode,
                                        xx_memory_map *output,
                                        xx_pd_struct *pd);

/* --- Atari ST Getters & Properties --- */
XXFC_API uint16_t xx_atarist_get_magic(const xx_atarist *atarist);
XXFC_API uint32_t xx_atarist_get_text_size(const xx_atarist *atarist);
XXFC_API uint32_t xx_atarist_get_data_size(const xx_atarist *atarist);
XXFC_API uint32_t xx_atarist_get_bss_size(const xx_atarist *atarist);
XXFC_API uint32_t xx_atarist_get_symbol_size(const xx_atarist *atarist);
XXFC_API uint32_t xx_atarist_get_reserved(const xx_atarist *atarist);
XXFC_API uint32_t xx_atarist_get_flags(const xx_atarist *atarist);
XXFC_API uint16_t xx_atarist_get_relocation(const xx_atarist *atarist);

/** Offset of the TEXT segment relative to the format base address. */
XXFC_API int64_t xx_atarist_get_text_offset(const xx_atarist *atarist);
/** Offset of the DATA segment relative to the format base address. */
XXFC_API int64_t xx_atarist_get_data_offset(const xx_atarist *atarist);
/** Offset of the symbol table relative to the format base address, or -1. */
XXFC_API int64_t xx_atarist_get_symbol_offset(const xx_atarist *atarist);
/** Offset of the relocation table relative to the base address, or -1. */
XXFC_API int64_t xx_atarist_get_relocation_offset(const xx_atarist *atarist);
/** TEXT + DATA + BSS, the size of the loaded image. */
XXFC_API int64_t xx_atarist_get_image_size(const xx_atarist *atarist);

/* --- Atari ST Setters --- */
XXFC_API void xx_atarist_set_text_size(xx_atarist *atarist, uint32_t val);
XXFC_API void xx_atarist_set_data_size(xx_atarist *atarist, uint32_t val);
XXFC_API void xx_atarist_set_bss_size(xx_atarist *atarist, uint32_t val);
XXFC_API void xx_atarist_set_symbol_size(xx_atarist *atarist, uint32_t val);
XXFC_API void xx_atarist_set_flags(xx_atarist *atarist, uint32_t val);
XXFC_API void xx_atarist_set_relocation(xx_atarist *atarist, uint16_t val);

/* Cast helpers */
static inline Abstractformat *xx_atarist_to_format(xx_atarist *atarist) {
    return atarist ? &atarist->format : NULL;
}

static inline const Abstractformat *xx_atarist_to_format_const(const xx_atarist *atarist) {
    return atarist ? &atarist->format : NULL;
}

/* User-facing aliases without xx_ prefix */
static inline void XAtariST_init(xx_atarist *atarist, xx_io_device *dev, int64_t base_address) {
    xx_atarist_init(atarist, dev, base_address);
}

static inline xx_atarist *XAtariST_create(xx_io_device *dev, int64_t base_address) {
    return xx_atarist_create(dev, base_address);
}

static inline void XAtariST_free(xx_atarist *atarist) {
    xx_atarist_free(atarist);
}

static inline bool XAtariST_check_is_valid(xx_atarist *atarist, xx_pd_struct *pd) {
    return atarist ? xx_atarist_check_is_valid(&atarist->format, pd) : false;
}

static inline bool XAtariST_handle_base_info(xx_atarist *atarist, xx_pd_struct *pd) {
    return atarist ? xx_atarist_handle_base_info(&atarist->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_ATARIST_H */
