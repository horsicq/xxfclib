/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_PC_86F_H
#define XX_PC_86F_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_pc_86f;
typedef xx_pc_86f xx_pc_86f_t;
XXFC_API void xx_pc_86f_init(xx_pc_86f *,xx_io_device *,int64_t);
XXFC_API xx_pc_86f *xx_pc_86f_create(xx_io_device *,int64_t);
XXFC_API void xx_pc_86f_destroy(xx_pc_86f *);
XXFC_API void xx_pc_86f_free(xx_pc_86f *);
XXFC_API bool xx_pc_86f_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pc_86f_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
