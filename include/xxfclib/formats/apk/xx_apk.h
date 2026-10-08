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

/**
 * @brief APK format object.
 *
 * The embedded xx_zip object must remain first: all inherited archive,
 * packing, extraction, and data-structure callbacks operate on it directly.
 */
struct xx_apk {
    xx_zip zip;
    char *manifest_text;
};

XXFC_API void xx_apk_init(xx_apk *apk, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_apk *xx_apk_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_apk_free(xx_apk *apk);
XXFC_API void xx_apk_destroy(xx_apk *apk);

XXFC_API bool xx_apk_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apk_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apk_analyze(xx_apk *apk, xx_pd_struct *pd);
XXFC_API const char *xx_apk_get_manifest(const xx_apk *apk);
XXFC_API char *xx_apk_manifest_record(const xx_apk *apk, const char *key);

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

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_apk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_apk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_apk_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_APK_H */
