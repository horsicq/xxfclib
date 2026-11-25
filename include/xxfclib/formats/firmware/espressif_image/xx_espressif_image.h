/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component extraction. ESP32 chip ID 0 with no appended SHA digest; verifies segment XOR checksum. No flash or firmware execution.
 */
#ifndef XX_ESPRESSIF_IMAGE_H
#define XX_ESPRESSIF_IMAGE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_espressif_image {
    Abstractformat format;
} xx_espressif_image;
XXFC_API void xx_espressif_image_init(xx_espressif_image *, xx_io_device *, int64_t);
XXFC_API xx_espressif_image *xx_espressif_image_create(xx_io_device *, int64_t);
XXFC_API void xx_espressif_image_destroy(xx_espressif_image *);
XXFC_API void xx_espressif_image_free(xx_espressif_image *);
XXFC_API bool xx_espressif_image_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_espressif_image_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_espressif_image_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_espressif_image_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_espressif_image_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_espressif_image_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_espressif_image_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
