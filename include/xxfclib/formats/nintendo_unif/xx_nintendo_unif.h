/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_NINTENDO_UNIF_H
#define XX_NINTENDO_UNIF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_unif { Abstractformat format; } xx_nintendo_unif;
XXFC_API void xx_nintendo_unif_init(xx_nintendo_unif *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_unif *xx_nintendo_unif_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_unif_destroy(xx_nintendo_unif *);
XXFC_API void xx_nintendo_unif_free(xx_nintendo_unif *);
XXFC_API bool xx_nintendo_unif_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_unif_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_unif_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_unif_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_unif_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_unif_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_unif_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
