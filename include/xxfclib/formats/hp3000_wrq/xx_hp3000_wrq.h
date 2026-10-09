/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://www.microfocus.com/documentation/amc-archive/infoconnect-16-2/pdfdoc/infoconnect-help/infoconnect-help.pdf
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_HP3000_WRQ_H
#define XX_HP3000_WRQ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hp3000_wrq { Abstractformat format; } xx_hp3000_wrq;
XXFC_API void xx_hp3000_wrq_init(xx_hp3000_wrq *,xx_io_device *,int64_t);
XXFC_API xx_hp3000_wrq *xx_hp3000_wrq_create(xx_io_device *,int64_t);
XXFC_API void xx_hp3000_wrq_destroy(xx_hp3000_wrq *);
XXFC_API void xx_hp3000_wrq_free(xx_hp3000_wrq *);
XXFC_API bool xx_hp3000_wrq_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_hp3000_wrq_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_hp3000_wrq_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_hp3000_wrq_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_hp3000_wrq_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_hp3000_wrq_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_hp3000_wrq_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
