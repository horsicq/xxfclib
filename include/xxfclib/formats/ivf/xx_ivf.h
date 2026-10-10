/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * IVF (DKIF) video packet container reader.
 * Format reference: https://github.com/webmproject/libvpx/blob/main/ivfenc.c
 * Decoder reference: https://github.com/webmproject/libvpx/blob/main/ivfdec.c
 */
#ifndef XX_IVF_H
#define XX_IVF_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_ivf {
    Abstractformat format;
} xx_ivf;

XXFC_API void xx_ivf_init(xx_ivf *, xx_io_device *, int64_t);
XXFC_API xx_ivf *xx_ivf_create(xx_io_device *, int64_t);
XXFC_API void xx_ivf_destroy(xx_ivf *);
XXFC_API void xx_ivf_free(xx_ivf *);
XXFC_API bool xx_ivf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ivf_handle_base_info(Abstractformat *, xx_pd_struct *);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ivf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ivf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ivf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ivf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ivf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
