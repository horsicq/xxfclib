/* SPDX-License-Identifier: MIT
 * Wire specification: https://raw.githubusercontent.com/dranjan/python-plyfile/master/plyfile.py */
#ifndef XX_POLYGON_PLY_H
#define XX_POLYGON_PLY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_polygon_ply { Abstractformat format; } xx_polygon_ply;
XXFC_API void xx_polygon_ply_init(xx_polygon_ply *,xx_io_device *,int64_t);
XXFC_API xx_polygon_ply *xx_polygon_ply_create(xx_io_device *,int64_t);
XXFC_API void xx_polygon_ply_destroy(xx_polygon_ply *);
XXFC_API void xx_polygon_ply_free(xx_polygon_ply *);
XXFC_API bool xx_polygon_ply_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_polygon_ply_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
