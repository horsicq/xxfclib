/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only CD-i Green Book filesystem reader. No payload is executed.
 */
#ifndef XX_CDI_FS_H
#define XX_CDI_FS_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_cdi_fs;
typedef xx_cdi_fs xx_cdi_fs_t;
XXFC_API void xx_cdi_fs_init(xx_cdi_fs *,xx_io_device *,int64_t);
XXFC_API xx_cdi_fs *xx_cdi_fs_create(xx_io_device *,int64_t);
XXFC_API void xx_cdi_fs_destroy(xx_cdi_fs *);
XXFC_API void xx_cdi_fs_free(xx_cdi_fs *);
XXFC_API bool xx_cdi_fs_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cdi_fs_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_cdi_fs_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_cdi_fs_to_format(xx_cdi_fs *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

