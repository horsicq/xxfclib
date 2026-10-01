/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/project-gemmi/gemmi/master/include/gemmi/dsn6.hpp
 * DSN6/BRIX density-map family: complete typed binary/ASCII descriptor, valid positive grid/cell/scaling parameters and exact8x8x8 brick extents. Binary reserved descriptor words must be zero; brick padding outside declared extents is retained unchanged. Original descriptor and encoded density bricks exported; no resampling and no external map dependencies.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_DSN6_DENSITY_H
#define XX_DSN6_DENSITY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dsn6_density {Abstractformat format;} xx_dsn6_density;
XXFC_API void xx_dsn6_density_init(xx_dsn6_density *,xx_io_device *,int64_t);
XXFC_API xx_dsn6_density *xx_dsn6_density_create(xx_io_device *,int64_t);
XXFC_API void xx_dsn6_density_destroy(xx_dsn6_density *);
XXFC_API void xx_dsn6_density_free(xx_dsn6_density *);
XXFC_API bool xx_dsn6_density_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dsn6_density_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
