/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_FEI_H
#define XX_FEI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_fei {
    Abstractformat format;
} xx_fei;
XXFC_API void xx_fei_init(xx_fei *, xx_io_device *, int64_t);
XXFC_API xx_fei *xx_fei_create(xx_io_device *, int64_t);
XXFC_API void xx_fei_destroy(xx_fei *);
XXFC_API void xx_fei_free(xx_fei *);
XXFC_API bool xx_fei_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_fei_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
