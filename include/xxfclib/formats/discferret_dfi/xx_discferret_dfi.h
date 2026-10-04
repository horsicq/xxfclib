/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_DISCFERRET_DFI_H
#define XX_DISCFERRET_DFI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_discferret_dfi {Abstractformat format;} xx_discferret_dfi;
XXFC_API void xx_discferret_dfi_init(xx_discferret_dfi *,xx_io_device *,int64_t);
XXFC_API xx_discferret_dfi *xx_discferret_dfi_create(xx_io_device *,int64_t);
XXFC_API void xx_discferret_dfi_destroy(xx_discferret_dfi *);
XXFC_API void xx_discferret_dfi_free(xx_discferret_dfi *);
XXFC_API bool xx_discferret_dfi_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_discferret_dfi_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
