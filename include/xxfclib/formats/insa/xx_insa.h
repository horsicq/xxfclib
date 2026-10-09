/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMATS_INSA_XX_INSA_H
#define XXFCLIB_FORMATS_INSA_XX_INSA_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/** INSA version-1 concatenation of length-prefixed LH1 streams. */
typedef struct xx_insa {
    Abstractformat format;
} xx_insa;

XXFC_API void xx_insa_init(xx_insa *archive, xx_io_device *device,
                            int64_t base_address);
XXFC_API xx_insa *xx_insa_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_insa_destroy(xx_insa *archive);
XXFC_API void xx_insa_free(xx_insa *archive);
XXFC_API bool xx_insa_check_is_valid(Abstractformat *format, xx_pd_struct *pd);
XXFC_API bool xx_insa_handle_base_info(Abstractformat *format,
                                        xx_pd_struct *pd);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_insa_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_insa_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_insa_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_insa_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_insa_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
