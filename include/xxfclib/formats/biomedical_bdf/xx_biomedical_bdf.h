/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.biosemi.com/faq/file_format.htm */
#ifndef XX_BIOMEDICAL_BDF_H
#define XX_BIOMEDICAL_BDF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_biomedical_bdf { Abstractformat format; } xx_biomedical_bdf;
XXFC_API void xx_biomedical_bdf_init(xx_biomedical_bdf *,xx_io_device *,int64_t);
XXFC_API xx_biomedical_bdf *xx_biomedical_bdf_create(xx_io_device *,int64_t);
XXFC_API void xx_biomedical_bdf_destroy(xx_biomedical_bdf *);
XXFC_API void xx_biomedical_bdf_free(xx_biomedical_bdf *);
XXFC_API bool xx_biomedical_bdf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_biomedical_bdf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
