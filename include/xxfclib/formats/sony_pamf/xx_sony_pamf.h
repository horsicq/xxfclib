/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/rpcs3/rpcs3/master/rpcs3/Emu/Cell/Modules/cellPamf.h
 * PAMF0040/0041 with one grouping period/group, up to32 stream descriptors and bounded12-byte entry-point tables. Exports stream metadata, entry points and encoded MPEG
 * program-stream body; no codec decoding/demultiplexing, PSMF marks, trust or playback.
 */
#ifndef XX_SONY_PAMF_H
#define XX_SONY_PAMF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sony_pamf {
    Abstractformat format;
} xx_sony_pamf;
XXFC_API void xx_sony_pamf_init(xx_sony_pamf *, xx_io_device *, int64_t);
XXFC_API xx_sony_pamf *xx_sony_pamf_create(xx_io_device *, int64_t);
XXFC_API void xx_sony_pamf_destroy(xx_sony_pamf *);
XXFC_API void xx_sony_pamf_free(xx_sony_pamf *);
XXFC_API bool xx_sony_pamf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sony_pamf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sony_pamf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sony_pamf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sony_pamf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sony_pamf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sony_pamf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
