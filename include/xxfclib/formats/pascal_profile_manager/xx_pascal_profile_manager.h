/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only Pascal ProFile Manager reader. No payload is executed.
 */
#ifndef XX_PASCAL_PROFILE_MANAGER_H
#define XX_PASCAL_PROFILE_MANAGER_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_pascal_profile_manager;
typedef xx_pascal_profile_manager xx_pascal_profile_manager_t;
XXFC_API void xx_pascal_profile_manager_init(xx_pascal_profile_manager *,xx_io_device *,int64_t);
XXFC_API xx_pascal_profile_manager *xx_pascal_profile_manager_create(xx_io_device *,int64_t);
XXFC_API void xx_pascal_profile_manager_destroy(xx_pascal_profile_manager *);
XXFC_API void xx_pascal_profile_manager_free(xx_pascal_profile_manager *);
XXFC_API bool xx_pascal_profile_manager_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pascal_profile_manager_handle_base_info(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pascal_profile_manager_extract_record_to_device(Abstractformat *,xx_archive_record_state *,xx_io_device *,xx_pd_struct *);
static inline Abstractformat *xx_pascal_profile_manager_to_format(xx_pascal_profile_manager *r) { return r ? &r->format : NULL; }
#ifdef __cplusplus
}
#endif
#endif

