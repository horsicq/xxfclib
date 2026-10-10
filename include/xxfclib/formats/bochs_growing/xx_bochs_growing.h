/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_BOCHS_GROWING_H
#define XX_BOCHS_GROWING_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_bochs_growing;
typedef xx_bochs_growing xx_bochs_growing_t;
XXFC_API void xx_bochs_growing_init(xx_bochs_growing *, xx_io_device *, int64_t);
XXFC_API xx_bochs_growing *xx_bochs_growing_create(xx_io_device *, int64_t);
XXFC_API void xx_bochs_growing_destroy(xx_bochs_growing *);
XXFC_API void xx_bochs_growing_free(xx_bochs_growing *);
XXFC_API bool xx_bochs_growing_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_bochs_growing_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
