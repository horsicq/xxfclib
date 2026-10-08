/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMATS_DN_XX_DN_H
#define XXFCLIB_FORMATS_DN_XX_DN_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** DOS Navigator 1.x archive with explicit data and directory records. */
typedef struct xx_dn {
    Abstractformat format;
} xx_dn;

XXFC_API void xx_dn_init(xx_dn *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_dn *xx_dn_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_dn_destroy(xx_dn *archive);
XXFC_API void xx_dn_free(xx_dn *archive);
XXFC_API bool xx_dn_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_dn_handle_base_info(Abstractformat *format, xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dn_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dn_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dn_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
