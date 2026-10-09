/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/rmdec.c
 * RealMedia v0 RMF/PROP/CONT/MDPR/DATA and optional INDX, complete object table, typed stream descriptors and packet lengths/stream references. Original metadata and encoded packets are exported, including the original FFmpeg DATA-size-plus18/eight-zero-byte-footer convention; RMFFv1, multirate/linked DATA sections, encrypted/LIVE layouts and codec decoding are unsupported.
 * File limit64MiB, member limit4096. No payload or external resource is executed.
 */
#ifndef XX_REALMEDIA_RM_H
#define XX_REALMEDIA_RM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_realmedia_rm { Abstractformat format; } xx_realmedia_rm;
XXFC_API void xx_realmedia_rm_init(xx_realmedia_rm *,xx_io_device *,int64_t);
XXFC_API xx_realmedia_rm *xx_realmedia_rm_create(xx_io_device *,int64_t);
XXFC_API void xx_realmedia_rm_destroy(xx_realmedia_rm *);
XXFC_API void xx_realmedia_rm_free(xx_realmedia_rm *);
XXFC_API bool xx_realmedia_rm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_realmedia_rm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_realmedia_rm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_realmedia_rm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_realmedia_rm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_realmedia_rm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_realmedia_rm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
