/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_FORMAT_LX_H
#define XXFCLIB_FORMAT_LX_H

#include "xxfclib/formats/le/xx_le.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef xx_linear_executable xx_lx;
typedef xx_linear_executable xx_lx_t;
typedef xx_linear_executable XLX;

XXFC_API void xx_lx_init(xx_lx *lx, xx_io_device *device,
                         int64_t base_address);
XXFC_API xx_lx *xx_lx_create(xx_io_device *device, int64_t base_address);
XXFC_API void xx_lx_destroy(xx_lx *lx);
XXFC_API void xx_lx_free(xx_lx *lx);

XXFC_API bool xx_lx_check_is_valid(Abstractformat *self, xx_pd_struct *pd);
XXFC_API bool xx_lx_handle_base_info(Abstractformat *self, xx_pd_struct *pd);
XXFC_API int64_t xx_lx_get_format_size(Abstractformat *self,
                                       xx_pd_struct *pd);
XXFC_API uint64_t xx_lx_get_number_of_imports(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_lx_get_number_of_exports(Abstractformat *self,
                                              xx_pd_struct *pd);
XXFC_API uint64_t xx_lx_get_number_of_resources(Abstractformat *self,
                                                xx_pd_struct *pd);
XXFC_API bool xx_lx_get_memory_map(Abstractformat *self,
                                   xx_memory_map_mode_t mode,
                                   xx_memory_map *output,
                                   xx_pd_struct *pd);

XXFC_API uint32_t xx_lx_get_number_of_objects(const xx_lx *lx);
XXFC_API const xx_linear_object *xx_lx_get_object(const xx_lx *lx,
                                                  uint32_t index);

static inline Abstractformat *xx_lx_to_format(xx_lx *lx) {
    return lx ? &lx->format : NULL;
}

static inline const Abstractformat *xx_lx_to_format_const(const xx_lx *lx) {
    return lx ? &lx->format : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XXFCLIB_FORMAT_LX_H */
