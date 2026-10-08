/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.faccts.de/docs/orca/6.1/manual/contents/essentialelements/input.html */
#ifndef XX_ORCA_INPUT_H
#define XX_ORCA_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_orca_input { Abstractformat format; } xx_orca_input;
XXFC_API void xx_orca_input_init(xx_orca_input *,xx_io_device *,int64_t);
XXFC_API xx_orca_input *xx_orca_input_create(xx_io_device *,int64_t);
XXFC_API void xx_orca_input_destroy(xx_orca_input *);
XXFC_API void xx_orca_input_free(xx_orca_input *);
XXFC_API bool xx_orca_input_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_orca_input_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_orca_input_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_orca_input_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_orca_input_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
