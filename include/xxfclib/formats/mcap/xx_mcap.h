/* SPDX-License-Identifier: MIT
 * Wire specification: https://mcap.dev/spec */
#ifndef XX_MCAP_H
#define XX_MCAP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mcap { Abstractformat format; } xx_mcap;
XXFC_API void xx_mcap_init(xx_mcap *,xx_io_device *,int64_t);
XXFC_API xx_mcap *xx_mcap_create(xx_io_device *,int64_t);
XXFC_API void xx_mcap_destroy(xx_mcap *);
XXFC_API void xx_mcap_free(xx_mcap *);
XXFC_API bool xx_mcap_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mcap_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mcap_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_mcap_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mcap_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mcap_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_mcap_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
