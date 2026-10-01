/* SPDX-License-Identifier: MIT
 * Independently implemented from https://www.mathworks.com/help/pdf_doc/matlab/matfile_format.pdf
 * Bounded encoded-component extraction. */
#ifndef XX_MATLAB_MAT5_H
#define XX_MATLAB_MAT5_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_matlab_mat5 { Abstractformat format; } xx_matlab_mat5;
XXFC_API void xx_matlab_mat5_init(xx_matlab_mat5 *,xx_io_device *,int64_t);
XXFC_API xx_matlab_mat5 *xx_matlab_mat5_create(xx_io_device *,int64_t);
XXFC_API void xx_matlab_mat5_destroy(xx_matlab_mat5 *);
XXFC_API void xx_matlab_mat5_free(xx_matlab_mat5 *);
XXFC_API bool xx_matlab_mat5_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_matlab_mat5_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
