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
typedef struct xx_espressif_image { Abstractformat format; } xx_espressif_image;
XXFC_API void xx_espressif_image_init(xx_espressif_image *,xx_io_device *,int64_t);
XXFC_API xx_espressif_image *xx_espressif_image_create(xx_io_device *,int64_t);
XXFC_API void xx_espressif_image_destroy(xx_espressif_image *);
XXFC_API void xx_espressif_image_free(xx_espressif_image *);
XXFC_API bool xx_espressif_image_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_espressif_image_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
