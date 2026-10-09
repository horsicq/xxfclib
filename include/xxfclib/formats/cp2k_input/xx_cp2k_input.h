/* SPDX-License-Identifier: MIT
 * Wire specification: https://manual.cp2k.org/trunk/CP2K_INPUT.html */
#ifndef XX_CP2K_INPUT_H
#define XX_CP2K_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_cp2k_input { Abstractformat format; } xx_cp2k_input;
XXFC_API void xx_cp2k_input_init(xx_cp2k_input *,xx_io_device *,int64_t);
XXFC_API xx_cp2k_input *xx_cp2k_input_create(xx_io_device *,int64_t);
XXFC_API void xx_cp2k_input_destroy(xx_cp2k_input *);
XXFC_API void xx_cp2k_input_free(xx_cp2k_input *);
XXFC_API bool xx_cp2k_input_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cp2k_input_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_cp2k_input_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_cp2k_input_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_cp2k_input_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_cp2k_input_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_cp2k_input_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
