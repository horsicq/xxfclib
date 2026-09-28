/* SPDX-License-Identifier: MIT
 * Wire specification: https://grischa-hahn.hier-im-netz.de/astro/ser/SER%20Doc%20V2.pdf */
#ifndef XX_ASTRONOMY_SER_H
#define XX_ASTRONOMY_SER_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_astronomy_ser { Abstractformat format; } xx_astronomy_ser;
XXFC_API void xx_astronomy_ser_init(xx_astronomy_ser *,xx_io_device *,int64_t);
XXFC_API xx_astronomy_ser *xx_astronomy_ser_create(xx_io_device *,int64_t);
XXFC_API void xx_astronomy_ser_destroy(xx_astronomy_ser *);
XXFC_API void xx_astronomy_ser_free(xx_astronomy_ser *);
XXFC_API bool xx_astronomy_ser_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_astronomy_ser_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
