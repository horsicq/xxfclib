/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/mpegts.c
 * 188-byte MPEG transport packets, one program, single-packet PAT/PMT and optional SDT sections with complete lengths and MPEG CRC32. Packet/adaptation/PES-start framing, PID declarations and continuity are checked; at most4096 packets. Original encoded transport packets are exported;192/204-byte variants, fragmented tables, encryption, multiple programs and codec decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_MPEG_TRANSPORT_STREAM_H
#define XX_MPEG_TRANSPORT_STREAM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mpeg_transport_stream { Abstractformat format; } xx_mpeg_transport_stream;
XXFC_API void xx_mpeg_transport_stream_init(xx_mpeg_transport_stream *,xx_io_device *,int64_t);
XXFC_API xx_mpeg_transport_stream *xx_mpeg_transport_stream_create(xx_io_device *,int64_t);
XXFC_API void xx_mpeg_transport_stream_destroy(xx_mpeg_transport_stream *);
XXFC_API void xx_mpeg_transport_stream_free(xx_mpeg_transport_stream *);
XXFC_API bool xx_mpeg_transport_stream_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mpeg_transport_stream_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
