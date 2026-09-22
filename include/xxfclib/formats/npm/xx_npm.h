/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_npm.h
 * @brief npm package implemented as a gzip-compressed TAR-derived format.
 */

#ifndef XXFCLIB_FORMAT_NPM_H
#define XXFCLIB_FORMAT_NPM_H

#include "xxfclib/formats/tar_gz/xx_tar_gz.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_npm xx_npm;
typedef struct xx_npm xx_npm_t;
typedef struct xx_npm XNPM;

/**
 * @brief npm package format object.
 *
 * The embedded xx_tar_gz object must remain first so all inherited tar.gz
 * archive, packing, extraction, and data-structure callbacks operate directly.
 */
struct xx_npm {
    xx_tar_gz tar_gz;
};

XXFC_API void xx_npm_init(xx_npm *npm, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_npm *xx_npm_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_npm_free(xx_npm *npm);
XXFC_API void xx_npm_destroy(xx_npm *npm);

XXFC_API bool xx_npm_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_npm_handle_base_info(Abstractformat *self, xx_pd_struct *pd);

static inline Abstractformat *xx_npm_to_format(xx_npm *npm) {
    return npm ? &npm->tar_gz.format : NULL;
}

static inline const Abstractformat *xx_npm_to_format_const(const xx_npm *npm) {
    return npm ? &npm->tar_gz.format : NULL;
}

static inline xx_tar_gz *xx_npm_to_tar_gz(xx_npm *npm) {
    return npm ? &npm->tar_gz : NULL;
}

static inline const xx_tar_gz *xx_npm_to_tar_gz_const(const xx_npm *npm) {
    return npm ? &npm->tar_gz : NULL;
}

static inline void XNPM_init(xx_npm *npm, xx_io_device *dev,
                             int64_t base_address) {
    xx_npm_init(npm, dev, base_address);
}

static inline xx_npm *XNPM_create(xx_io_device *dev, int64_t base_address) {
    return xx_npm_create(dev, base_address);
}

static inline void XNPM_free(xx_npm *npm) {
    xx_npm_free(npm);
}

static inline bool XNPM_is_valid(xx_npm *npm, xx_pd_struct *pd) {
    return npm ? xx_format_is_valid(&npm->tar_gz.format, pd) : false;
}

static inline bool XNPM_handle_base_info(xx_npm *npm, xx_pd_struct *pd) {
    return npm ? xx_format_handle_base_info(&npm->tar_gz.format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_NPM_H */
