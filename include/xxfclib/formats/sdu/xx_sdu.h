/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_SDU_H
#define XX_SDU_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sdu {Abstractformat format;} xx_sdu;
XXFC_API void xx_sdu_init(xx_sdu *,xx_io_device *,int64_t);
XXFC_API xx_sdu *xx_sdu_create(xx_io_device *,int64_t);
XXFC_API void xx_sdu_destroy(xx_sdu *);
XXFC_API void xx_sdu_free(xx_sdu *);
XXFC_API bool xx_sdu_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sdu_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
