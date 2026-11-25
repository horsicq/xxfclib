/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/ClassicUO/ClassicUO/blob/master/src/ClassicUO.IO/UOFileUop.cs
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_MYTHIC_MYP_H
#define XX_MYTHIC_MYP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mythic_myp {
    Abstractformat format;
} xx_mythic_myp;
XXFC_API void xx_mythic_myp_init(xx_mythic_myp *, xx_io_device *, int64_t);
XXFC_API xx_mythic_myp *xx_mythic_myp_create(xx_io_device *, int64_t);
XXFC_API void xx_mythic_myp_destroy(xx_mythic_myp *);
XXFC_API void xx_mythic_myp_free(xx_mythic_myp *);
XXFC_API bool xx_mythic_myp_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_mythic_myp_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mythic_myp_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mythic_myp_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mythic_myp_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mythic_myp_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mythic_myp_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
