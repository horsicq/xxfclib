/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/mpeg.c
 * MPEG1/2 program packs, bounded system/stream-map/PES headers (PTS/DTS optional fields only) and complete declared packet lengths; stream-map CRC bytes remain encoded. Encoded packets are exported; clean packet-boundary EOF or a terminal program-end marker is required. Zero-length PES, encryption, DVD private substream interpretation and elementary codec decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_MPEG_PROGRAM_STREAM_H
#define XX_MPEG_PROGRAM_STREAM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mpeg_program_stream { Abstractformat format; } xx_mpeg_program_stream;
XXFC_API void xx_mpeg_program_stream_init(xx_mpeg_program_stream *,xx_io_device *,int64_t);
XXFC_API xx_mpeg_program_stream *xx_mpeg_program_stream_create(xx_io_device *,int64_t);
XXFC_API void xx_mpeg_program_stream_destroy(xx_mpeg_program_stream *);
XXFC_API void xx_mpeg_program_stream_free(xx_mpeg_program_stream *);
XXFC_API bool xx_mpeg_program_stream_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mpeg_program_stream_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
