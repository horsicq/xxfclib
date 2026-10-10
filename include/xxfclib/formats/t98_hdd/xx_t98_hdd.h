/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_T98_HDD_H
#define XX_T98_HDD_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_t98_hdd;
typedef xx_t98_hdd xx_t98_hdd_t;
XXFC_API void xx_t98_hdd_init(xx_t98_hdd *, xx_io_device *, int64_t);
XXFC_API xx_t98_hdd *xx_t98_hdd_create(xx_io_device *, int64_t);
XXFC_API void xx_t98_hdd_destroy(xx_t98_hdd *);
XXFC_API void xx_t98_hdd_free(xx_t98_hdd *);
XXFC_API bool xx_t98_hdd_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_t98_hdd_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
