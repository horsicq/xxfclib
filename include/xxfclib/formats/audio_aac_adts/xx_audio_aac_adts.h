/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/adts_header.c
 * MPEG4 AAC-LC ADTS, unprotected seven-byte headers and one raw-data block per frame, stable standard sample rate/channel configuration1-7. Complete declared frame extents are checked; original ADTS frames are exported. MPEG2, CRC-protected/multiple-block frames, explicit program configurations, ID3 wrappers and AAC element decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_AUDIO_AAC_ADTS_H
#define XX_AUDIO_AAC_ADTS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_aac_adts { Abstractformat format; } xx_audio_aac_adts;
XXFC_API void xx_audio_aac_adts_init(xx_audio_aac_adts *,xx_io_device *,int64_t);
XXFC_API xx_audio_aac_adts *xx_audio_aac_adts_create(xx_io_device *,int64_t);
XXFC_API void xx_audio_aac_adts_destroy(xx_audio_aac_adts *);
XXFC_API void xx_audio_aac_adts_free(xx_audio_aac_adts *);
XXFC_API bool xx_audio_aac_adts_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_audio_aac_adts_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
