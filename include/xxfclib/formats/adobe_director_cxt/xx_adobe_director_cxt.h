/* SPDX-License-Identifier: MIT. Bounded native adobe_director_cxt reader. */
#ifndef XX_ADOBE_DIRECTOR_CXT_H
#define XX_ADOBE_DIRECTOR_CXT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_adobe_director_cxt { Abstractformat format; } xx_adobe_director_cxt;
XXFC_API void xx_adobe_director_cxt_init(xx_adobe_director_cxt *,xx_io_device *,int64_t);
XXFC_API xx_adobe_director_cxt *xx_adobe_director_cxt_create(xx_io_device *,int64_t);
XXFC_API void xx_adobe_director_cxt_destroy(xx_adobe_director_cxt *);
XXFC_API void xx_adobe_director_cxt_free(xx_adobe_director_cxt *);
XXFC_API bool xx_adobe_director_cxt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adobe_director_cxt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
