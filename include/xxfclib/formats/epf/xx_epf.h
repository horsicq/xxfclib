/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMATS_EPF_XX_EPF_H
#define XXFCLIB_FORMATS_EPF_XX_EPF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** East Point EPFS archive. The FAT follows the member payloads. */
typedef struct xx_epf {
    Abstractformat format;
} xx_epf;

XXFC_API void xx_epf_init(xx_epf *archive, xx_io_device *device,
                          int64_t base_address);
XXFC_API xx_epf *xx_epf_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_epf_destroy(xx_epf *archive);
XXFC_API void xx_epf_free(xx_epf *archive);
XXFC_API bool xx_epf_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_epf_handle_base_info(Abstractformat *format,
                                      xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_epf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_epf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_epf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
