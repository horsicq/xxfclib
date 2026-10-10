/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_FDX68_FDX_H
#define XX_FDX68_FDX_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_fdx68_fdx;
typedef xx_fdx68_fdx xx_fdx68_fdx_t;
XXFC_API void xx_fdx68_fdx_init(xx_fdx68_fdx *, xx_io_device *, int64_t);
XXFC_API xx_fdx68_fdx *xx_fdx68_fdx_create(xx_io_device *, int64_t);
XXFC_API void xx_fdx68_fdx_destroy(xx_fdx68_fdx *);
XXFC_API void xx_fdx68_fdx_free(xx_fdx68_fdx *);
XXFC_API bool xx_fdx68_fdx_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_fdx68_fdx_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
