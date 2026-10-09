/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/stepcode/stepcode/develop/src/clstepcore/read_func.cc
 * STEP Part21 cleartext edition1/2 framing: mandatory typed HEADER records, complete DATA entities/recursive parameters, unique positive IDs and resolved local references. Original encoded descriptor/entity records exported. Schema-specific CAD evaluation, SCOPE, ANCHOR/REFERENCE/signature sections and encoded-string directives declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_STEP_PART21_H
#define XX_STEP_PART21_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_step_part21 {Abstractformat format;} xx_step_part21;
XXFC_API void xx_step_part21_init(xx_step_part21 *,xx_io_device *,int64_t);
XXFC_API xx_step_part21 *xx_step_part21_create(xx_io_device *,int64_t);
XXFC_API void xx_step_part21_destroy(xx_step_part21 *);
XXFC_API void xx_step_part21_free(xx_step_part21 *);
XXFC_API bool xx_step_part21_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_step_part21_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_step_part21_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_step_part21_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_step_part21_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_step_part21_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_step_part21_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
