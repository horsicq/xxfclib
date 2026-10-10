/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_HEATHKIT_H17_H
#define XX_HEATHKIT_H17_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_heathkit_h17;
typedef xx_heathkit_h17 xx_heathkit_h17_t;
XXFC_API void xx_heathkit_h17_init(xx_heathkit_h17 *, xx_io_device *, int64_t);
XXFC_API xx_heathkit_h17 *xx_heathkit_h17_create(xx_io_device *, int64_t);
XXFC_API void xx_heathkit_h17_destroy(xx_heathkit_h17 *);
XXFC_API void xx_heathkit_h17_free(xx_heathkit_h17 *);
XXFC_API bool xx_heathkit_h17_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_heathkit_h17_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
