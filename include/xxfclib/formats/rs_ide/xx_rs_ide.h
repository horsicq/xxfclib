/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_RS_IDE_H
#define XX_RS_IDE_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_rs_ide;
typedef xx_rs_ide xx_rs_ide_t;
XXFC_API void xx_rs_ide_init(xx_rs_ide *,xx_io_device *,int64_t);
XXFC_API xx_rs_ide *xx_rs_ide_create(xx_io_device *,int64_t);
XXFC_API void xx_rs_ide_destroy(xx_rs_ide *);
XXFC_API void xx_rs_ide_free(xx_rs_ide *);
XXFC_API bool xx_rs_ide_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_rs_ide_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
