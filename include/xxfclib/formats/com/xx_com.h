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
 * @file xx_com.h
 * @brief DOS .COM executable format implementation.
 *
 * A .COM file has NO header and NO signature of any kind.  It is a raw 16-bit
 * code image that DOS loads at offset 0x100 of a freshly allocated segment,
 * directly behind the 256-byte Program Segment Prefix, and enters at CS:0x100.
 * Consequently there is nothing in the file content that identifies it, and
 * `xx_com_check_is_valid` is a *size bound plus a negative test*, not a
 * signature match.  See the notes above `xx_com_check_is_valid` in xx_com.c
 * for exactly how weak this is and where it may be used.
 */

#ifndef XXFCLIB_FORMAT_COM_H
#define XXFCLIB_FORMAT_COM_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations and type aliases */
typedef struct xx_com xx_com;
typedef struct xx_com xx_com_t;
typedef struct xx_com XCom;

/** Load address of the image: the byte right after the 256-byte PSP. */
#define XX_COM_ADDRESS_BEGIN UINT32_C(0x100)
/** Size of the single 64 KiB segment a COM program owns. */
#define XX_COM_IMAGE_SIZE    UINT32_C(0x10000)
/** Largest file that can still be loaded at 0x100 inside one segment. */
#define XX_COM_MAX_FILE_SIZE (XX_COM_IMAGE_SIZE - XX_COM_ADDRESS_BEGIN)

/**
 * @brief Concrete DOS .COM executable format structure.
 * Inherits from Abstractformat by placing it as the first member.
 *
 * None of the members below are parsed header fields -- the format has no
 * header.  They are the derived quantities the reference (XCOM / XCOM_DEF)
 * exposes: the fixed load address, the fixed image size, the file-derived
 * code size, and the first two bytes of the entry point, which the reference
 * surfaces as its only "record" (`EntryBytes`).
 */
struct xx_com {
    Abstractformat format;        /**< Base format structure (first member) */
    uint32_t       address_begin; /**< Load address, always XX_COM_ADDRESS_BEGIN */
    uint32_t       image_size;    /**< Segment size, always XX_COM_IMAGE_SIZE */
    int64_t        code_size;     /**< Bytes of the file mapped as code */
    uint16_t       entry_bytes;   /**< First two bytes at the entry point */
};

/* --- Constructors & Lifecycle --- */
XXFC_API void xx_com_init(xx_com *com, xx_io_device *dev, int64_t base_address);
XXFC_API xx_com *xx_com_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_com_destroy(xx_com *com);
XXFC_API void xx_com_free(xx_com *com);

/* --- Format Implementation Callbacks --- */
XXFC_API bool xx_com_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_com_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_com_get_format_size(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_com_get_memory_map(Abstractformat *self,
                                    xx_memory_map_mode_t mode,
                                    xx_memory_map *output,
                                    xx_pd_struct *pd);

/* --- COM Getters & Properties --- */
XXFC_API uint32_t xx_com_get_address_begin(const xx_com *com);
XXFC_API uint32_t xx_com_get_image_size(const xx_com *com);
XXFC_API int64_t xx_com_get_code_size(const xx_com *com);
XXFC_API uint16_t xx_com_get_entry_bytes(const xx_com *com);
XXFC_API uint64_t xx_com_get_entry_point_address(const xx_com *com);

/* Cast helpers */
static inline Abstractformat *xx_com_to_format(xx_com *com) {
    return com ? &com->format : NULL;
}

static inline const Abstractformat *xx_com_to_format_const(const xx_com *com) {
    return com ? &com->format : NULL;
}

/* User-facing aliases without xx_ prefix */
static inline void XCom_init(xx_com *com, xx_io_device *dev, int64_t base_address) {
    xx_com_init(com, dev, base_address);
}

static inline xx_com *XCom_create(xx_io_device *dev, int64_t base_address) {
    return xx_com_create(dev, base_address);
}

static inline void XCom_free(xx_com *com) {
    xx_com_free(com);
}

static inline bool XCom_check_is_valid(xx_com *com, xx_pd_struct *pd) {
    return com ? xx_com_check_is_valid(&com->format, pd) : false;
}

static inline bool XCom_handle_base_info(xx_com *com, xx_pd_struct *pd) {
    return com ? xx_com_handle_base_info(&com->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_COM_H */
