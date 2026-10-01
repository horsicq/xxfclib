/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.openfoam.com/documentation/user-guide/4-mesh-generation-and-conversion/4.1-mesh-description */
#ifndef XX_OPENFOAM_POINTS_H
#define XX_OPENFOAM_POINTS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_openfoam_points { Abstractformat format; } xx_openfoam_points;
XXFC_API void xx_openfoam_points_init(xx_openfoam_points *,xx_io_device *,int64_t);
XXFC_API xx_openfoam_points *xx_openfoam_points_create(xx_io_device *,int64_t);
XXFC_API void xx_openfoam_points_destroy(xx_openfoam_points *);
XXFC_API void xx_openfoam_points_free(xx_openfoam_points *);
XXFC_API bool xx_openfoam_points_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_openfoam_points_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
