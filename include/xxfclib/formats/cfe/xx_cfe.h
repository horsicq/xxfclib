/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_cfe.h @brief Broadcom CFE (Common Firmware Environment)
 *  bootloader image, found by its "CFE1CFE1" seal. */

/* This is NOT a container format and it has no header with lengths in it.
 * A CFE image is the raw boot ROM of a Broadcom router, cable modem or
 * set-top box: it starts with the CPU's reset vectors, and a little way in,
 * at a fixed offset from the start of the image, it carries the eight ASCII
 * bytes "CFE1CFE1" - the entry-point seal that operating-system loaders use
 * to recognise the firmware they were started from.
 *
 *   +0x00  reset / API-entry vectors (CPU instructions, not parsed)
 *   +0x1C  "CFE1CFE1", eight ASCII bytes, no terminator
 *   ...    code, data, often an NVRAM area; nothing says where it ends
 *
 * Source: binwalk's src/signatures/cfe.rs, which is the only module that
 * format has - there is no structures/cfe.rs and no extractors/cfe.rs.  That
 * parser accepts the seal whenever it sits at least 28 bytes into the data,
 * reports the image as starting 28 bytes before it, performs no further
 * validation, and reports no size.  This reader applies exactly that rule at
 * its base address: the seal must be at base_address + 28.
 *
 * SIZE.  binwalk returns size 0 for this signature, which its scanner then
 * resolves to "up to the next signature of at least medium confidence, or to
 * end of file".  A reader here sees only the device it is handed, so the
 * image is everything from base_address to the end of the device and there
 * is never an overlay.  For a CFE image carved out of a full flash dump that
 * is the same number binwalk produces; for the whole dump it is not, because
 * the following partitions are not this reader's to recognise.
 *
 * WHAT THIS READER DOES NOT DO.  It publishes no archive records (binwalk
 * extracts nothing from CFE either), and it does not guess the CPU or the
 * byte order: CFE exists for big- and little-endian MIPS and for ARM, and
 * the seal is the same ASCII string in all of them.
 */

#ifndef XXFCLIB_FORMAT_CFE_H
#define XXFCLIB_FORMAT_CFE_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The entry-point seal, eight ASCII bytes. */
#define XX_CFE_MAGIC "CFE1CFE1"
#define XX_CFE_MAGIC_SIZE 8U
/** Offset of the seal from the start of the image (binwalk: 28). */
#define XX_CFE_MAGIC_OFFSET 28U
/** Smallest image that can hold the seal: 28 + 8 bytes. */
#define XX_CFE_MIN_SIZE (XX_CFE_MAGIC_OFFSET + XX_CFE_MAGIC_SIZE)

typedef struct xx_cfe xx_cfe;
typedef struct xx_cfe xx_cfe_t;
typedef struct xx_cfe XCfe;

struct xx_cfe {
    Abstractformat format;
    int64_t seal_offset; /**< Absolute offset of "CFE1CFE1", or -1. */
    int64_t image_size;  /**< base_address .. end of device, or -1. */
};

XXFC_API void xx_cfe_init(xx_cfe *cfe, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_cfe *xx_cfe_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_cfe_destroy(xx_cfe *cfe);
XXFC_API void xx_cfe_free(xx_cfe *cfe);

XXFC_API bool xx_cfe_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_cfe_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_cfe_get_format_size(Abstractformat *self,
                                        xx_pd_struct *pd);

XXFC_API int64_t xx_cfe_get_seal_offset(const xx_cfe *cfe);
XXFC_API int64_t xx_cfe_get_image_size(const xx_cfe *cfe);

static inline Abstractformat *xx_cfe_to_format(xx_cfe *cfe) {
    return cfe ? &cfe->format : NULL;
}
static inline void XCfe_init(xx_cfe *cfe, xx_io_device *dev,
                             int64_t base_address) {
    xx_cfe_init(cfe, dev, base_address);
}
static inline xx_cfe *XCfe_create(xx_io_device *dev, int64_t base_address) {
    return xx_cfe_create(dev, base_address);
}
static inline void XCfe_free(xx_cfe *cfe) { xx_cfe_free(cfe); }
static inline bool XCfe_is_valid(xx_cfe *cfe, xx_pd_struct *pd) {
    return cfe ? xx_format_is_valid(&cfe->format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_CFE_H */
