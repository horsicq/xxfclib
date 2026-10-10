/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_apks.h
 * @brief Android APK Set (bundletool .apks), a validated ZIP-derived package.
 */
#ifndef XXFCLIB_FORMAT_APKS_H
#define XXFCLIB_FORMAT_APKS_H

#include "xxfclib/formats/zip/xx_zip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_apks { xx_zip zip; } xx_apks;
typedef xx_apks xx_apks_t;
typedef xx_apks XAPKS;

XXFC_API void xx_apks_init(xx_apks *apks, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_apks *xx_apks_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_apks_destroy(xx_apks *apks);
XXFC_API void xx_apks_free(xx_apks *apks);
XXFC_API bool xx_apks_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_apks_handle_base_info(Abstractformat *self, xx_pd_struct *pd);

/** Validate a CRC-authenticated toc.pb and at least one complete inner APK.
 * The borrowed device's cursor is restored on success and failure. The probe
 * accepts stored/deflated identity entries, visits at most 20,000 outer records
 * and eight APK candidates, and bounds TOC/deflated APK payloads to 16/64 MiB.
 * Stored APKs use a bounded view and streaming CRC verification.
 */
XXFC_API xx_file_type_t xx_apks_detect(xx_io_device *device, int64_t base_address);

static inline Abstractformat *xx_apks_to_format(xx_apks *apks) {
    return apks ? &apks->zip.format : NULL;
}
static inline const Abstractformat *xx_apks_to_format_const(const xx_apks *apks) {
    return apks ? &apks->zip.format : NULL;
}
static inline xx_zip *xx_apks_to_zip(xx_apks *apks) {
    return apks ? &apks->zip : NULL;
}

#ifdef __cplusplus
}
#endif
#endif /* XXFCLIB_FORMAT_APKS_H */
