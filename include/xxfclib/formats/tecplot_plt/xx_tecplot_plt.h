/* SPDX-License-Identifier: MIT
 * Wire specification: https://tecplot.azureedge.net/products/360/2024r1m1/360-data-format.html */
#ifndef XX_TECPLOT_PLT_H
#define XX_TECPLOT_PLT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tecplot_plt { Abstractformat format; } xx_tecplot_plt;
XXFC_API void xx_tecplot_plt_init(xx_tecplot_plt *,xx_io_device *,int64_t);
XXFC_API xx_tecplot_plt *xx_tecplot_plt_create(xx_io_device *,int64_t);
XXFC_API void xx_tecplot_plt_destroy(xx_tecplot_plt *);
XXFC_API void xx_tecplot_plt_free(xx_tecplot_plt *);
XXFC_API bool xx_tecplot_plt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tecplot_plt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tecplot_plt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tecplot_plt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tecplot_plt_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
