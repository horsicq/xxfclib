/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_jar.h
 * @brief Java archive format implemented as a ZIP-derived format.
 */

#ifndef XXFCLIB_FORMAT_JAR_H
#define XXFCLIB_FORMAT_JAR_H

#include "xxfclib/formats/zip/xx_zip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_jar xx_jar;
typedef struct xx_jar xx_jar_t;
typedef struct xx_jar XJAR;

/**
 * @brief JAR format object.
 *
 * The embedded xx_zip object must remain first: all inherited archive,
 * packing, extraction, and data-structure callbacks operate on it directly.
 */
struct xx_jar {
    xx_zip zip;
};

XXFC_API void xx_jar_init(xx_jar *jar, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_jar *xx_jar_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_jar_free(xx_jar *jar);
XXFC_API void xx_jar_destroy(xx_jar *jar);

XXFC_API bool xx_jar_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_jar_handle_base_info(Abstractformat *self, xx_pd_struct *pd);

static inline Abstractformat *xx_jar_to_format(xx_jar *jar) {
    return jar ? &jar->zip.format : NULL;
}

static inline const Abstractformat *xx_jar_to_format_const(const xx_jar *jar) {
    return jar ? &jar->zip.format : NULL;
}

static inline xx_zip *xx_jar_to_zip(xx_jar *jar) {
    return jar ? &jar->zip : NULL;
}

static inline const xx_zip *xx_jar_to_zip_const(const xx_jar *jar) {
    return jar ? &jar->zip : NULL;
}

static inline void XJAR_init(xx_jar *jar, xx_io_device *dev,
                             int64_t base_address) {
    xx_jar_init(jar, dev, base_address);
}

static inline xx_jar *XJAR_create(xx_io_device *dev, int64_t base_address) {
    return xx_jar_create(dev, base_address);
}

static inline void XJAR_free(xx_jar *jar) {
    xx_jar_free(jar);
}

static inline bool XJAR_is_valid(xx_jar *jar, xx_pd_struct *pd) {
    return jar ? xx_format_is_valid(&jar->zip.format, pd) : false;
}

static inline bool XJAR_handle_base_info(xx_jar *jar, xx_pd_struct *pd) {
    return jar ? xx_format_handle_base_info(&jar->zip.format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_JAR_H */
