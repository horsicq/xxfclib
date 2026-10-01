/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_NINTENDO_FDS_H
#define XX_NINTENDO_FDS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_fds { Abstractformat format; } xx_nintendo_fds;
XXFC_API void xx_nintendo_fds_init(xx_nintendo_fds *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_fds *xx_nintendo_fds_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_fds_destroy(xx_nintendo_fds *);
XXFC_API void xx_nintendo_fds_free(xx_nintendo_fds *);
XXFC_API bool xx_nintendo_fds_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_fds_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
