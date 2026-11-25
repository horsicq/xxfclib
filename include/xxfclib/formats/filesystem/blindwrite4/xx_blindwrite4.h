/* SPDX-License-Identifier: MIT. Native bounded image reader. */
#ifndef XX_BLINDWRITE4_H
#define XX_BLINDWRITE4_H
#include "xxfclib/formats/disk_additions/xx_disk_additions.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_disk_additions_info xx_blindwrite4;
typedef xx_blindwrite4 xx_blindwrite4_t;
XXFC_API void xx_blindwrite4_init(xx_blindwrite4 *, xx_io_device *, int64_t);
XXFC_API xx_blindwrite4 *xx_blindwrite4_create(xx_io_device *, int64_t);
XXFC_API void xx_blindwrite4_destroy(xx_blindwrite4 *);
XXFC_API void xx_blindwrite4_free(xx_blindwrite4 *);
XXFC_API bool xx_blindwrite4_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_blindwrite4_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API uint32_t xx_blindwrite4_open_data_files(xx_blindwrite4 *, const char *descriptor_path);
XXFC_API void xx_blindwrite4_set_companions(xx_blindwrite4 *, xx_io_device *, xx_io_device *);
#ifdef __cplusplus
}
#endif
#endif
