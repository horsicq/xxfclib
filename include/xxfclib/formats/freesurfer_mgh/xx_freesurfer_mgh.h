/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/freesurfer/freesurfer/dev/matlab/load_mgh.m */
#ifndef XX_FREESURFER_MGH_H
#define XX_FREESURFER_MGH_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_freesurfer_mgh { Abstractformat format; } xx_freesurfer_mgh;
XXFC_API void xx_freesurfer_mgh_init(xx_freesurfer_mgh *,xx_io_device *,int64_t);
XXFC_API xx_freesurfer_mgh *xx_freesurfer_mgh_create(xx_io_device *,int64_t);
XXFC_API void xx_freesurfer_mgh_destroy(xx_freesurfer_mgh *);
XXFC_API void xx_freesurfer_mgh_free(xx_freesurfer_mgh *);
XXFC_API bool xx_freesurfer_mgh_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_freesurfer_mgh_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
