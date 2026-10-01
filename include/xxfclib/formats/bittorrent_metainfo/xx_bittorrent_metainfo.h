/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_BITTORRENT_METAINFO_H
#define XX_BITTORRENT_METAINFO_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_bittorrent_metainfo { Abstractformat format; } xx_bittorrent_metainfo;
XXFC_API void xx_bittorrent_metainfo_init(xx_bittorrent_metainfo *,xx_io_device *,int64_t);
XXFC_API xx_bittorrent_metainfo *xx_bittorrent_metainfo_create(xx_io_device *,int64_t);
XXFC_API void xx_bittorrent_metainfo_destroy(xx_bittorrent_metainfo *);
XXFC_API void xx_bittorrent_metainfo_free(xx_bittorrent_metainfo *);
XXFC_API bool xx_bittorrent_metainfo_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_bittorrent_metainfo_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
