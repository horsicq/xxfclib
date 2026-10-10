/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/scummvm/scummvm/master/engines/mohawk/resource.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_MOHAWK_MHK_H
#define XX_MOHAWK_MHK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mohawk_mhk {
    Abstractformat format;
} xx_mohawk_mhk;
XXFC_API void xx_mohawk_mhk_init(xx_mohawk_mhk *, xx_io_device *, int64_t);
XXFC_API xx_mohawk_mhk *xx_mohawk_mhk_create(xx_io_device *, int64_t);
XXFC_API void xx_mohawk_mhk_destroy(xx_mohawk_mhk *);
XXFC_API void xx_mohawk_mhk_free(xx_mohawk_mhk *);
XXFC_API bool xx_mohawk_mhk_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_mohawk_mhk_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mohawk_mhk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mohawk_mhk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mohawk_mhk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mohawk_mhk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mohawk_mhk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
