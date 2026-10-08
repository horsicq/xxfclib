/* SPDX-License-Identifier: MIT
 * Wire specification: https://pyscf.org/_modules/pyscf/tools/cubegen.html */
#ifndef XX_GAUSSIAN_CUBE_H
#define XX_GAUSSIAN_CUBE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gaussian_cube { Abstractformat format; } xx_gaussian_cube;
XXFC_API void xx_gaussian_cube_init(xx_gaussian_cube *,xx_io_device *,int64_t);
XXFC_API xx_gaussian_cube *xx_gaussian_cube_create(xx_io_device *,int64_t);
XXFC_API void xx_gaussian_cube_destroy(xx_gaussian_cube *);
XXFC_API void xx_gaussian_cube_free(xx_gaussian_cube *);
XXFC_API bool xx_gaussian_cube_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gaussian_cube_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gaussian_cube_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gaussian_cube_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gaussian_cube_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
