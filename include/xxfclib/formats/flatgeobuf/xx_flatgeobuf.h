/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_FLATGEOBUF_H
#define XX_FLATGEOBUF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_flatgeobuf {
    Abstractformat format;
} xx_flatgeobuf;
XXFC_API void xx_flatgeobuf_init(xx_flatgeobuf *, xx_io_device *, int64_t);
XXFC_API xx_flatgeobuf *xx_flatgeobuf_create(xx_io_device *, int64_t);
XXFC_API void xx_flatgeobuf_destroy(xx_flatgeobuf *);
XXFC_API void xx_flatgeobuf_free(xx_flatgeobuf *);
XXFC_API bool xx_flatgeobuf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_flatgeobuf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_flatgeobuf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_flatgeobuf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_flatgeobuf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_flatgeobuf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_flatgeobuf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
