/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_apk.h
 * @brief Android package format implemented as a ZIP-derived format.
 */

#ifndef XXFCLIB_FORMAT_APK_H
#define XXFCLIB_FORMAT_APK_H

#include "xxfclib/formats/zip/xx_zip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_apk xx_apk;
typedef struct xx_apk xx_apk_t;
typedef struct xx_apk XAPK;

/**
 * @brief APK format object.
 *
 * The embedded xx_zip object must remain first: all inherited archive,
 * packing, extraction, and data-structure callbacks operate on it directly.
 */
struct xx_apk {
    xx_zip zip;
};

XXFC_API void xx_apk_init(xx_apk *apk, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_apk *xx_apk_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_apk_free(xx_apk *apk);
XXFC_API void xx_apk_destroy(xx_apk *apk);

XXFC_API bool xx_apk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);

static inline Abstractformat *xx_apk_to_format(xx_apk *apk) {
    return apk ? &apk->zip.format : NULL;
}

static inline const Abstractformat *xx_apk_to_format_const(const xx_apk *apk) {
    return apk ? &apk->zip.format : NULL;
}

static inline xx_zip *xx_apk_to_zip(xx_apk *apk) {
    return apk ? &apk->zip : NULL;
}

static inline const xx_zip *xx_apk_to_zip_const(const xx_apk *apk) {
    return apk ? &apk->zip : NULL;
}

static inline void XAPK_init(xx_apk *apk, xx_io_device *dev,
                             int64_t base_address) {
    xx_apk_init(apk, dev, base_address);
}

static inline xx_apk *XAPK_create(xx_io_device *dev, int64_t base_address) {
    return xx_apk_create(dev, base_address);
}

static inline void XAPK_free(xx_apk *apk) {
    xx_apk_free(apk);
}

static inline bool XAPK_is_valid(xx_apk *apk, xx_pd_struct *pd) {
    return apk ? xx_format_is_valid(&apk->zip.format, pd) : false;
}

static inline bool XAPK_handle_base_info(xx_apk *apk, xx_pd_struct *pd) {
    return apk ? xx_format_handle_base_info(&apk->zip.format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_APK_H */
