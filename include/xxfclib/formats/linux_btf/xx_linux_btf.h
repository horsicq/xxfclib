/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_LINUX_BTF_H
#define XX_LINUX_BTF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_linux_btf { Abstractformat format; } xx_linux_btf;
XXFC_API void xx_linux_btf_init(xx_linux_btf *,xx_io_device *,int64_t);
XXFC_API xx_linux_btf *xx_linux_btf_create(xx_io_device *,int64_t);
XXFC_API void xx_linux_btf_destroy(xx_linux_btf *);
XXFC_API void xx_linux_btf_free(xx_linux_btf *);
XXFC_API bool xx_linux_btf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_linux_btf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_linux_btf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_linux_btf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_linux_btf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_linux_btf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_linux_btf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
