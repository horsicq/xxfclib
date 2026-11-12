/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XXFCLIB_FORMAT_SCUMMVM_DXA_H
#define XXFCLIB_FORMAT_SCUMMVM_DXA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** ScummVM/Feeble Files DEXA video, distinct from DxLib DXA archives.
 * Exports the embedded RIFF/WAVE and decoded, indexed-color BMP frames.
 * Supported video methods: zlib keyframes (2), XOR deltas (3), unchanged
 * frames; CMAP palette carry-over; normal, doubled and interlaced heights.
 * Interlaced output uses palette index zero on the unencoded scanlines,
 * matching the format's indexed surface convention.
 * Methods 12/13 and extended headers are explicitly unsupported.
 * Bounds: width/height <=2048, <=65535 frames, compressed frame <=16 MiB.
 * Each frame is a palette tag (CMAP/NULL), then a picture tag (FRAM/NULL).
 * Only frame buffers and the bounded frame index are retained in memory.
 */
typedef struct xx_scummvm_dxa {
    Abstractformat format;
    uint32_t width, height, frame_count;
    uint8_t flags;
} xx_scummvm_dxa;
XXFC_API void xx_scummvm_dxa_init(xx_scummvm_dxa *, xx_io_device *, int64_t);
XXFC_API xx_scummvm_dxa *xx_scummvm_dxa_create(xx_io_device *, int64_t);
XXFC_API void xx_scummvm_dxa_destroy(xx_scummvm_dxa *);
XXFC_API void xx_scummvm_dxa_free(xx_scummvm_dxa *);
XXFC_API bool xx_scummvm_dxa_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_scummvm_dxa_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_scummvm_dxa_get_format_size(Abstractformat *, xx_pd_struct *);
XXFC_API uint64_t xx_scummvm_dxa_get_number_of_archive_records(Abstractformat *, xx_pd_struct *);
XXFC_API xx_archive_record_state *xx_scummvm_dxa_create_archive_records_reading(Abstractformat *, const xx_list_s *, xx_pd_struct *);
XXFC_API const xx_archive_record *xx_scummvm_dxa_get_current_archive_record(Abstractformat *, xx_archive_record_state *);
XXFC_API bool xx_scummvm_dxa_unpack_current_archive_record(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API bool xx_scummvm_dxa_archive_record_move_to_next(Abstractformat *, xx_archive_record_state *, xx_pd_struct *);
XXFC_API void xx_scummvm_dxa_free_archive_records_reading(Abstractformat *, xx_archive_record_state *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_scummvm_dxa_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_scummvm_dxa_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_scummvm_dxa_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_scummvm_dxa_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_scummvm_dxa_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
