/* SPDX-License-Identifier: MIT
 * Independently implemented from https://raw.githubusercontent.com/OSGeo/shapelib/master/dbfopen.c
 * Bounded encoded-component extraction. */
#ifndef XX_DBASE_DBF_H
#define XX_DBASE_DBF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dbase_dbf { Abstractformat format; } xx_dbase_dbf;
XXFC_API void xx_dbase_dbf_init(xx_dbase_dbf *,xx_io_device *,int64_t);
XXFC_API xx_dbase_dbf *xx_dbase_dbf_create(xx_io_device *,int64_t);
XXFC_API void xx_dbase_dbf_destroy(xx_dbase_dbf *);
XXFC_API void xx_dbase_dbf_free(xx_dbase_dbf *);
XXFC_API bool xx_dbase_dbf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dbase_dbf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
