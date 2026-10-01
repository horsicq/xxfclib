/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/antangelo/xdvdfs/main/xdvdfs-core/src/layout/volume.rs
 * XDVDFS descriptor at sector32, flat root directory with up to1024 stored files. Validates both descriptor signatures, child offsets/cycles, directory-record extents and disjoint file data. Exports files with numeric names; nested directories, full-disc security sectors and ISO hybrids unsupported.
 */
#ifndef XX_XBOX_XDVDFS_H
#define XX_XBOX_XDVDFS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_xbox_xdvdfs { Abstractformat format; } xx_xbox_xdvdfs;
XXFC_API void xx_xbox_xdvdfs_init(xx_xbox_xdvdfs *,xx_io_device *,int64_t);
XXFC_API xx_xbox_xdvdfs *xx_xbox_xdvdfs_create(xx_io_device *,int64_t);
XXFC_API void xx_xbox_xdvdfs_destroy(xx_xbox_xdvdfs *);
XXFC_API void xx_xbox_xdvdfs_free(xx_xbox_xdvdfs *);
XXFC_API bool xx_xbox_xdvdfs_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_xbox_xdvdfs_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
