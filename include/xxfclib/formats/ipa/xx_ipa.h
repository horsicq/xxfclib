/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/**
 * @file xx_ipa.h
 * @brief Apple IPA package implemented as a ZIP-derived format.
 */

#ifndef XXFCLIB_FORMAT_IPA_H
#define XXFCLIB_FORMAT_IPA_H

#include "xxfclib/formats/zip/xx_zip.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ipa xx_ipa;
typedef struct xx_ipa xx_ipa_t;
typedef struct xx_ipa XIPA;

/**
 * @brief IPA format object.
 *
 * The embedded xx_zip object must remain first so all inherited ZIP archive,
 * packing, extraction, and data-structure callbacks can operate directly.
 */
struct xx_ipa {
    xx_zip zip;
};

XXFC_API void xx_ipa_init(xx_ipa *ipa, xx_io_device *dev,
                          int64_t base_address);
XXFC_API xx_ipa *xx_ipa_create(xx_io_device *dev, int64_t base_address);
XXFC_API void xx_ipa_free(xx_ipa *ipa);
XXFC_API void xx_ipa_destroy(xx_ipa *ipa);

XXFC_API bool xx_ipa_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_ipa_handle_base_info(Abstractformat *self, xx_pd_struct *pd);

static inline Abstractformat *xx_ipa_to_format(xx_ipa *ipa) {
    return ipa ? &ipa->zip.format : NULL;
}

static inline const Abstractformat *xx_ipa_to_format_const(const xx_ipa *ipa) {
    return ipa ? &ipa->zip.format : NULL;
}

static inline xx_zip *xx_ipa_to_zip(xx_ipa *ipa) {
    return ipa ? &ipa->zip : NULL;
}

static inline const xx_zip *xx_ipa_to_zip_const(const xx_ipa *ipa) {
    return ipa ? &ipa->zip : NULL;
}

static inline void XIPA_init(xx_ipa *ipa, xx_io_device *dev,
                             int64_t base_address) {
    xx_ipa_init(ipa, dev, base_address);
}

static inline xx_ipa *XIPA_create(xx_io_device *dev, int64_t base_address) {
    return xx_ipa_create(dev, base_address);
}

static inline void XIPA_free(xx_ipa *ipa) {
    xx_ipa_free(ipa);
}

static inline bool XIPA_is_valid(xx_ipa *ipa, xx_pd_struct *pd) {
    return ipa ? xx_format_is_valid(&ipa->zip.format, pd) : false;
}

static inline bool XIPA_handle_base_info(xx_ipa *ipa, xx_pd_struct *pd) {
    return ipa ? xx_format_handle_base_info(&ipa->zip.format, pd) : false;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_IPA_H */
