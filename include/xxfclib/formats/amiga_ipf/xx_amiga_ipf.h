/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_AMIGA_IPF_H
#define XX_AMIGA_IPF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amiga_ipf { Abstractformat format; } xx_amiga_ipf;
XXFC_API void xx_amiga_ipf_init(xx_amiga_ipf *,xx_io_device *,int64_t);
XXFC_API xx_amiga_ipf *xx_amiga_ipf_create(xx_io_device *,int64_t);
XXFC_API void xx_amiga_ipf_destroy(xx_amiga_ipf *);
XXFC_API void xx_amiga_ipf_free(xx_amiga_ipf *);
XXFC_API bool xx_amiga_ipf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amiga_ipf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
