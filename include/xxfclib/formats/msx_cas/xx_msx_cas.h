/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_MSX_CAS_H
#define XX_MSX_CAS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_msx_cas { Abstractformat format; } xx_msx_cas;
XXFC_API void xx_msx_cas_init(xx_msx_cas *,xx_io_device *,int64_t);
XXFC_API xx_msx_cas *xx_msx_cas_create(xx_io_device *,int64_t);
XXFC_API void xx_msx_cas_destroy(xx_msx_cas *);
XXFC_API void xx_msx_cas_free(xx_msx_cas *);
XXFC_API bool xx_msx_cas_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_msx_cas_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
