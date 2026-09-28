/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/ape.c
 * Monkey Audio3980/3990 modern descriptor and header, complete seek table, stored WAVE header/tail and encoded frame extents, plus typed optional APEv2 item tables. Integer8/16/24-bit mono/stereo and classic compression levels1000-5000 only. Encoded audio and its decoded CRC/descriptor MD5 remain uninterpreted; older layouts, unknown header extensions and decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_AUDIO_MONKEYS_APE_H
#define XX_AUDIO_MONKEYS_APE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_monkeys_ape { Abstractformat format; } xx_audio_monkeys_ape;
XXFC_API void xx_audio_monkeys_ape_init(xx_audio_monkeys_ape *,xx_io_device *,int64_t);
XXFC_API xx_audio_monkeys_ape *xx_audio_monkeys_ape_create(xx_io_device *,int64_t);
XXFC_API void xx_audio_monkeys_ape_destroy(xx_audio_monkeys_ape *);
XXFC_API void xx_audio_monkeys_ape_free(xx_audio_monkeys_ape *);
XXFC_API bool xx_audio_monkeys_ape_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_audio_monkeys_ape_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
