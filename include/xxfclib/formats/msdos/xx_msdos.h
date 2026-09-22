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
 * @file xx_msdos.h
 * @brief MS-DOS (MZ) executable format implementation.
 */

#ifndef XXFCLIB_FORMAT_MSDOS_H
#define XXFCLIB_FORMAT_MSDOS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations and type aliases */
typedef struct xx_msdos xx_msdos;
typedef struct xx_msdos xx_msdos_t;
typedef struct xx_msdos XMsdos;

/**
 * @brief Concrete MS-DOS/MZ executable format structure.
 * Inherits from Abstractformat by placing it as the first member.
 */
struct xx_msdos {
    Abstractformat format;              /**< Base format structure (first member) */
    uint16_t       signature;           /**< MZ signature (0x5A4D = 'MZ') */
    uint16_t       bytes_on_last_page;  /**< Bytes on last page of program */
    uint16_t       pages_in_file;       /**< Pages in file */
    uint16_t       relocations;         /**< Number of relocation table entries */
    uint16_t       header_size;         /**< Header size in paragraphs */
    uint16_t       minalloc;            /**< Minimum memory required in paragraphs */
    uint16_t       maxalloc;            /**< Maximum memory required in paragraphs */
    uint16_t       ss;                  /**< Initial SS (stack segment) */
    uint16_t       sp;                  /**< Initial SP (stack pointer) */
    uint16_t       checksum;            /**< Checksum (usually 0) */
    uint16_t       ip;                  /**< Initial IP (instruction pointer) */
    uint16_t       cs;                  /**< Initial CS (code segment) */
    uint16_t       reloc_offset;        /**< Offset to relocation table */
    uint16_t       overlay_number;      /**< Overlay number */
    int64_t        pe_offset;           /**< Offset to PE header (-1 if none) */
    bool           has_pe_header;       /**< True if PE header is present */
};

/* --- Constructors & Lifecycle --- */
XXFC_API void xx_msdos_init(xx_msdos *msdos, xx_io_device *dev, int64_t base_address);
XXFC_API xx_msdos *xx_msdos_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_msdos_free(xx_msdos *msdos);
XXFC_API void xx_msdos_destroy(xx_msdos *msdos);

/* --- Format Implementation Callbacks --- */
XXFC_API bool xx_msdos_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_msdos_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_msdos_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_msdos_get_memory_map(Abstractformat *self,
                                      xx_memory_map_mode_t mode,
                                      xx_memory_map *output,
                                      xx_pd_struct *pd);

/* --- MS-DOS Getters & Properties --- */
XXFC_API uint16_t xx_msdos_get_signature(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_bytes_on_last_page(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_pages_in_file(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_relocations(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_header_size(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_minalloc(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_maxalloc(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_ss(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_sp(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_checksum(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_ip(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_cs(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_reloc_offset(const xx_msdos *msdos);
XXFC_API uint16_t xx_msdos_get_overlay_number(const xx_msdos *msdos);
XXFC_API int64_t xx_msdos_get_pe_offset(const xx_msdos *msdos);
XXFC_API bool xx_msdos_has_pe_header(const xx_msdos *msdos);

/* --- MS-DOS Setters --- */
XXFC_API void xx_msdos_set_signature(xx_msdos *msdos, uint16_t val);
XXFC_API void xx_msdos_set_bytes_on_last_page(xx_msdos *msdos, uint16_t val);
XXFC_API void xx_msdos_set_pages_in_file(xx_msdos *msdos, uint16_t val);
XXFC_API void xx_msdos_set_relocations(xx_msdos *msdos, uint16_t val);
XXFC_API void xx_msdos_set_pe_offset(xx_msdos *msdos, int64_t val);
XXFC_API void xx_msdos_set_has_pe_header(xx_msdos *msdos, bool val);

/* Cast helpers */
static inline Abstractformat *xx_msdos_to_format(xx_msdos *msdos) {
    return msdos ? &msdos->format : NULL;
}

static inline const Abstractformat *xx_msdos_to_format_const(const xx_msdos *msdos) {
    return msdos ? &msdos->format : NULL;
}

/* User-facing aliases without xx_ prefix */
static inline void XMsdos_init(xx_msdos *msdos, xx_io_device *dev, int64_t base_address) {
    xx_msdos_init(msdos, dev, base_address);
}

static inline xx_msdos *XMsdos_create(xx_io_device *dev, int64_t base_address) {
    return xx_msdos_create(dev, base_address);
}

static inline void XMsdos_free(xx_msdos *msdos) {
    xx_msdos_free(msdos);
}

static inline bool XMsdos_check_is_valid(xx_msdos *msdos, xx_pd_struct *pd) {
    return msdos ? xx_msdos_check_is_valid(&msdos->format, pd) : false;
}

static inline bool XMsdos_handle_base_info(xx_msdos *msdos, xx_pd_struct *pd) {
    return msdos ? xx_msdos_handle_base_info(&msdos->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_MSDOS_H */
