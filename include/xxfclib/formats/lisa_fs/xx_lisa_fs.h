/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only Apple Lisa filesystem reader. No payload is executed.
 */
#ifndef XX_LISA_FS_H
#define XX_LISA_FS_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_lisa_fs;
typedef xx_lisa_fs xx_lisa_fs_t;
XXFC_API void xx_lisa_fs_init(xx_lisa_fs *, xx_io_device *, int64_t);
XXFC_API xx_lisa_fs *xx_lisa_fs_create(xx_io_device *, int64_t);
XXFC_API void xx_lisa_fs_destroy(xx_lisa_fs *);
XXFC_API void xx_lisa_fs_free(xx_lisa_fs *);
XXFC_API bool xx_lisa_fs_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_lisa_fs_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_lisa_fs_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
static inline Abstractformat *xx_lisa_fs_to_format(xx_lisa_fs *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
