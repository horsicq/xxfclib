/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.msg.chem.iastate.edu/gamess/GAMESS_Manual/input.pdf */
#ifndef XX_GAMESS_INPUT_H
#define XX_GAMESS_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gamess_input {
    Abstractformat format;
} xx_gamess_input;
XXFC_API void xx_gamess_input_init(xx_gamess_input *, xx_io_device *, int64_t);
XXFC_API xx_gamess_input *xx_gamess_input_create(xx_io_device *, int64_t);
XXFC_API void xx_gamess_input_destroy(xx_gamess_input *);
XXFC_API void xx_gamess_input_free(xx_gamess_input *);
XXFC_API bool xx_gamess_input_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_gamess_input_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gamess_input_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_gamess_input_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gamess_input_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gamess_input_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_gamess_input_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
