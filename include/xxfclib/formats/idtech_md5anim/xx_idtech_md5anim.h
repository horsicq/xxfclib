/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/DOOM-3/master/neo/game/anim/Anim.cpp
 * MD5Version10 animation text with strict complete hierarchy/bounds/baseframe/frame grammar. Validates one-root hierarchy, disjoint complete animated-component ranges, ordered frame IDs, finite values and reconstructed quaternion XYZ norms. Up to1024 frames/256 joints; original text components exported. Escaped/nonASCII names, comments after the final frame and playback unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_IDTECH_MD5ANIM_H
#define XX_IDTECH_MD5ANIM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_idtech_md5anim { Abstractformat format; } xx_idtech_md5anim;
XXFC_API void xx_idtech_md5anim_init(xx_idtech_md5anim *,xx_io_device *,int64_t);
XXFC_API xx_idtech_md5anim *xx_idtech_md5anim_create(xx_io_device *,int64_t);
XXFC_API void xx_idtech_md5anim_destroy(xx_idtech_md5anim *);
XXFC_API void xx_idtech_md5anim_free(xx_idtech_md5anim *);
XXFC_API bool xx_idtech_md5anim_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_idtech_md5anim_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
