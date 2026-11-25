/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_PSID_SID_H
#define XX_PSID_SID_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_psid_sid {
    Abstractformat format;
} xx_psid_sid;
XXFC_API void xx_psid_sid_init(xx_psid_sid *, xx_io_device *, int64_t);
XXFC_API xx_psid_sid *xx_psid_sid_create(xx_io_device *, int64_t);
XXFC_API void xx_psid_sid_destroy(xx_psid_sid *);
XXFC_API void xx_psid_sid_free(xx_psid_sid *);
XXFC_API bool xx_psid_sid_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_psid_sid_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_psid_sid_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_psid_sid_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_psid_sid_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_psid_sid_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_psid_sid_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
