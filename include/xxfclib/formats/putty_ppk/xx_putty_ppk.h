/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_PUTTY_PPK_H
#define XX_PUTTY_PPK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_putty_ppk {Abstractformat format;} xx_putty_ppk;
XXFC_API void xx_putty_ppk_init(xx_putty_ppk *,xx_io_device *,int64_t);
XXFC_API xx_putty_ppk *xx_putty_ppk_create(xx_io_device *,int64_t);
XXFC_API void xx_putty_ppk_destroy(xx_putty_ppk *);
XXFC_API void xx_putty_ppk_free(xx_putty_ppk *);
XXFC_API bool xx_putty_ppk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_putty_ppk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_putty_ppk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_putty_ppk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_putty_ppk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_putty_ppk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_putty_ppk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
