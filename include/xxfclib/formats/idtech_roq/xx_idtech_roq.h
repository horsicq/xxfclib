/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/idroqdec.c
 * RoQ video with complete info, VQ codebook/video and optional mono/stereo DPCM chunk extents. Geometry, codebook counts and VQ command references are bounded. Original coded chunks are exported; packet nesting, unknown chunks and video/audio rendering are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_IDTECH_ROQ_H
#define XX_IDTECH_ROQ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_idtech_roq { Abstractformat format; } xx_idtech_roq;
XXFC_API void xx_idtech_roq_init(xx_idtech_roq *,xx_io_device *,int64_t);
XXFC_API xx_idtech_roq *xx_idtech_roq_create(xx_io_device *,int64_t);
XXFC_API void xx_idtech_roq_destroy(xx_idtech_roq *);
XXFC_API void xx_idtech_roq_free(xx_idtech_roq *);
XXFC_API bool xx_idtech_roq_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_idtech_roq_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
