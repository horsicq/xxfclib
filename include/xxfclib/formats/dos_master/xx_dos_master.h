/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only DOS.MASTER disk layout reader. No payload is executed.
 */
#ifndef XX_DOS_MASTER_H
#define XX_DOS_MASTER_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_dos_master;
typedef xx_dos_master xx_dos_master_t;
XXFC_API void xx_dos_master_init(xx_dos_master *, xx_io_device *, int64_t);
XXFC_API xx_dos_master *xx_dos_master_create(xx_io_device *, int64_t);
XXFC_API void xx_dos_master_destroy(xx_dos_master *);
XXFC_API void xx_dos_master_free(xx_dos_master *);
XXFC_API bool xx_dos_master_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dos_master_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dos_master_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
static inline Abstractformat *xx_dos_master_to_format(xx_dos_master *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
