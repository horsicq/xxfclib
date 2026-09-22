/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_vxworks.h @brief VxWorks symbol table locator and enumerator. */

/* This is NOT an archive reader, and deliberately exposes no unpacking API.
 * A VxWorks symbol table is not a file format: it is a structure that lives
 * inside an already-loaded firmware image, and nothing in it is extractable.
 * What it is good for is telling you that an unknown blob is a VxWorks image
 * and handing you the symbol addresses, so this reader is a locator and an
 * enumerator, nothing more.
 *
 *   symbol table entry (16 bytes), repeated
 *     +0   u32  name pointer, a RUNTIME address into the loaded image
 *     +4   u32  symbol value, the runtime address the symbol denotes
 *     +8   u32  type:  0x500 function
 *                      0x700 initialized data
 *                      0x900 uninitialized data
 *     +12  u32  group
 *
 * Both fields at +0 and +4 are runtime addresses in the image's own address
 * space. The file offset those addresses correspond to depends on the load
 * base, which the table itself does not record, so SYMBOL NAMES ARE NOT
 * RESOLVED HERE - only the raw pointer is reported. Recovering the names
 * needs a load-base guess that belongs to an analysis layer, not a reader.
 *
 * Detection scans from base_address: the table is a run of entries whose type
 * field is one of the three known values and whose two pointers are non-zero.
 * Byte order is decided from the first entry's type field, which is 0x00000500
 * class and therefore distinguishable. A run shorter than the minimum entry
 * count is rejected, because three constrained words repeat by chance in
 * ordinary data often enough that a short run means nothing.
 */

#ifndef XXFCLIB_FORMAT_VXWORKS_H
#define XXFCLIB_FORMAT_VXWORKS_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Symbol type values carried in the entry's +8 word. */
#define XX_VXWORKS_SYMBOL_FUNCTION 0x500U
#define XX_VXWORKS_SYMBOL_INITIALIZED_DATA 0x700U
#define XX_VXWORKS_SYMBOL_UNINITIALIZED_DATA 0x900U

/** One decoded symbol table entry. */
typedef struct xx_vxworks_symbol_s {
    uint32_t name_pointer; /**< Runtime address of the name; NOT a file offset. */
    uint32_t value;        /**< Runtime address the symbol denotes. */
    uint32_t type;         /**< One of the XX_VXWORKS_SYMBOL_* values. */
    uint32_t group;
    int64_t entry_offset;  /**< Device offset of this 16-byte entry. */
} xx_vxworks_symbol;

typedef struct xx_vxworks xx_vxworks;
typedef struct xx_vxworks xx_vxworks_t;
typedef struct xx_vxworks XVxworks;

struct xx_vxworks {
    Abstractformat format;
    uint64_t number_of_symbols;
    int64_t table_offset;  /**< base_address, or -1 when nothing was found. */
    int64_t table_size;    /**< number_of_symbols * 16, or -1. */
    bool is_big_endian;
    void *internal;
};

XXFC_API void xx_vxworks_init(xx_vxworks *vxworks, xx_io_device *dev,
                              int64_t base_address);
XXFC_API xx_vxworks *xx_vxworks_create(xx_io_device *dev,
                                       int64_t base_address);
XXFC_API void xx_vxworks_destroy(xx_vxworks *vxworks);
XXFC_API void xx_vxworks_free(xx_vxworks *vxworks);

XXFC_API bool xx_vxworks_check_is_valid(Abstractformat *self,
                                        xx_pd_struct *pd);
XXFC_API bool xx_vxworks_handle_base_info(Abstractformat *self,
                                          xx_pd_struct *pd);
XXFC_API int64_t xx_vxworks_get_format_size(Abstractformat *self,
                                            xx_pd_struct *pd);
XXFC_API uint64_t xx_vxworks_get_number_of_metadata(Abstractformat *self,
                                                    xx_pd_struct *pd);

XXFC_API uint64_t xx_vxworks_get_number_of_symbols(const xx_vxworks *vxworks);
XXFC_API int64_t xx_vxworks_get_table_offset(const xx_vxworks *vxworks);
XXFC_API int64_t xx_vxworks_get_table_size(const xx_vxworks *vxworks);
XXFC_API bool xx_vxworks_get_is_big_endian(const xx_vxworks *vxworks);
/**
 * @brief Copy symbol @p index out of the parsed table.
 * @return false when the table has not been parsed or @p index is past its
 * end. Requires xx_vxworks_handle_base_info() to have succeeded.
 */
XXFC_API bool xx_vxworks_get_symbol(const xx_vxworks *vxworks, uint64_t index,
                                    xx_vxworks_symbol *output);
/** @brief "function", "initialized data", "uninitialized data", or NULL. */
XXFC_API const char *xx_vxworks_symbol_type_name(uint32_t type);

static inline Abstractformat *xx_vxworks_to_format(xx_vxworks *vxworks) {
    return vxworks ? &vxworks->format : NULL;
}
static inline void XVxworks_init(xx_vxworks *vxworks, xx_io_device *dev,
                                 int64_t base_address) {
    xx_vxworks_init(vxworks, dev, base_address);
}
static inline xx_vxworks *XVxworks_create(xx_io_device *dev,
                                          int64_t base_address) {
    return xx_vxworks_create(dev, base_address);
}
static inline void XVxworks_free(xx_vxworks *vxworks) {
    xx_vxworks_free(vxworks);
}
static inline bool XVxworks_is_valid(xx_vxworks *vxworks, xx_pd_struct *pd) {
    return vxworks ? xx_format_is_valid(&vxworks->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_VXWORKS_H */
