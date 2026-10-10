/* SPDX-License-Identifier: MIT. Bounded decoded multimedia adapter. */
#ifndef XX_MEDIA_ENGINE_H
#define XX_MEDIA_ENGINE_H
#include "xxfclib/formats/sevenzip_engine/xx_sevenzip_engine.h"
#ifdef __cplusplus
extern "C" {
#endif
/* mode is NULL (automatic), "audio", "video", or "frames".
 * Audio entries are decoded PCM16 WAV files. Video mode additionally exposes
 * playable lossless FFV1 Matroska video tracks. Frames mode exposes composited
 * PNG frames. The source device remains borrowed; TEST decodes into RAM only.
 * The separately licensed xfu_media_helper.exe and pinned FFmpeg DLLs must be
 * installed beside the application. Memory/password/member-size options use
 * the normal archive API. Audible AA derives its key from the file header.
 * Audible AAX takes an 8-digit hexadecimal activation key as the password;
 * MP4 CENC takes its 32-digit hexadecimal AES key. Missing/incorrect keys are
 * reported as password errors. Other DRM systems may remain unsupported.
 */
XXFC_API Abstractformat *xx_media_engine_create(xx_io_device *device, int64_t base_address, const char *mode);
XXFC_API void xx_media_engine_free(Abstractformat *format);
XXFC_API xx_sevenzip_backend_status xx_media_engine_get_status(const Abstractformat *format);
/* Returns the requested or detected mode; NULL before automatic detection. */
XXFC_API const char *xx_media_engine_get_mode(const Abstractformat *format);
/* Returns "mode:demuxer" after detection, or the requested mode beforehand. */
XXFC_API const char *xx_media_engine_get_details(const Abstractformat *format);
XXFC_API bool xx_media_engine_set_source_path(Abstractformat *format, const char *source_path);
#ifdef __cplusplus
}
#endif
#endif
