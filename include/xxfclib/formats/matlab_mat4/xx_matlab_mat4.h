/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.mathworks.com/help/pdf_doc/matlab/matfile_format.pdf */
#ifndef XX_MATLAB_MAT4_H
#define XX_MATLAB_MAT4_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_matlab_mat4 { Abstractformat format; } xx_matlab_mat4;
XXFC_API void xx_matlab_mat4_init(xx_matlab_mat4 *,xx_io_device *,int64_t);
XXFC_API xx_matlab_mat4 *xx_matlab_mat4_create(xx_io_device *,int64_t);
XXFC_API void xx_matlab_mat4_destroy(xx_matlab_mat4 *);
XXFC_API void xx_matlab_mat4_free(xx_matlab_mat4 *);
XXFC_API bool xx_matlab_mat4_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_matlab_mat4_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_matlab_mat4_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_matlab_mat4_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_matlab_mat4_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
