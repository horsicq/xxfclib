/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_CISCOPY_H
#define XX_CISCOPY_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_ciscopy;
typedef xx_ciscopy xx_ciscopy_t;
XXFC_API void xx_ciscopy_init(xx_ciscopy *, xx_io_device *, int64_t);
XXFC_API xx_ciscopy *xx_ciscopy_create(xx_io_device *, int64_t);
XXFC_API void xx_ciscopy_destroy(xx_ciscopy *);
XXFC_API void xx_ciscopy_free(xx_ciscopy *);
XXFC_API bool xx_ciscopy_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ciscopy_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
