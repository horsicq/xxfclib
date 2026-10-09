/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/0x0L/rs-utils/blob/master/bin/psarc.py
 * PSARC 1.4 unencrypted TOC and stored blocks only, including manifest member. zlib-compressed blocks and encrypted TOCs are rejected; original manifest names are not reconstructed.
 */
#ifndef XX_SONY_PSARC_H
#define XX_SONY_PSARC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sony_psarc { Abstractformat format; } xx_sony_psarc;
XXFC_API void xx_sony_psarc_init(xx_sony_psarc *,xx_io_device *,int64_t);
XXFC_API xx_sony_psarc *xx_sony_psarc_create(xx_io_device *,int64_t);
XXFC_API void xx_sony_psarc_destroy(xx_sony_psarc *);
XXFC_API void xx_sony_psarc_free(xx_sony_psarc *);
XXFC_API bool xx_sony_psarc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sony_psarc_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sony_psarc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sony_psarc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sony_psarc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sony_psarc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sony_psarc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
