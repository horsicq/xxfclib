/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_ORIC_DSK_H
#define XX_ORIC_DSK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_oric_dsk {Abstractformat format;} xx_oric_dsk;
XXFC_API void xx_oric_dsk_init(xx_oric_dsk *,xx_io_device *,int64_t);
XXFC_API xx_oric_dsk *xx_oric_dsk_create(xx_io_device *,int64_t);
XXFC_API void xx_oric_dsk_destroy(xx_oric_dsk *);
XXFC_API void xx_oric_dsk_free(xx_oric_dsk *);
XXFC_API bool xx_oric_dsk_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_oric_dsk_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
