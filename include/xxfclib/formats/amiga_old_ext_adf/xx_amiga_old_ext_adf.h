/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_AMIGA_OLD_EXT_ADF_H
#define XX_AMIGA_OLD_EXT_ADF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amiga_old_ext_adf {Abstractformat format;} xx_amiga_old_ext_adf;
XXFC_API void xx_amiga_old_ext_adf_init(xx_amiga_old_ext_adf *,xx_io_device *,int64_t);
XXFC_API xx_amiga_old_ext_adf *xx_amiga_old_ext_adf_create(xx_io_device *,int64_t);
XXFC_API void xx_amiga_old_ext_adf_destroy(xx_amiga_old_ext_adf *);
XXFC_API void xx_amiga_old_ext_adf_free(xx_amiga_old_ext_adf *);
XXFC_API bool xx_amiga_old_ext_adf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amiga_old_ext_adf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
