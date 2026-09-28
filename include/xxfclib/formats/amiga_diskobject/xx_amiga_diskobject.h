/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/aros-development-team/AROS/master/workbench/libs/icon/diskobjio.c
 * Classic Amiga68k DiskObject v1: complete Gadget flags, optional old/new drawer descriptor, planar Image descriptors/data, length-prefixed terminated default/tool/window strings and counted tooltypes. Original encoded icon components exported; NewIcons/ColorIcons/extra extensions and chained imagery declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_AMIGA_DISKOBJECT_H
#define XX_AMIGA_DISKOBJECT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amiga_diskobject {Abstractformat format;} xx_amiga_diskobject;
XXFC_API void xx_amiga_diskobject_init(xx_amiga_diskobject *,xx_io_device *,int64_t);
XXFC_API xx_amiga_diskobject *xx_amiga_diskobject_create(xx_io_device *,int64_t);
XXFC_API void xx_amiga_diskobject_destroy(xx_amiga_diskobject *);
XXFC_API void xx_amiga_diskobject_free(xx_amiga_diskobject *);
XXFC_API bool xx_amiga_diskobject_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amiga_diskobject_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
