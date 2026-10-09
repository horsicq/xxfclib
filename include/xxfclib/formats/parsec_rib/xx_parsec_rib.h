/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PARSEC_RIB_H
#define XX_PARSEC_RIB_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RIB\0 reverse-decoded Parsec resource stream. */
typedef struct xx_parsec_rib { Abstractformat format; } xx_parsec_rib;

XXFC_API void xx_parsec_rib_init(xx_parsec_rib *, xx_io_device *, int64_t);
XXFC_API xx_parsec_rib *xx_parsec_rib_create(xx_io_device *, int64_t);
XXFC_API void xx_parsec_rib_destroy(xx_parsec_rib *);
XXFC_API void xx_parsec_rib_free(xx_parsec_rib *);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_parsec_rib_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_parsec_rib_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_parsec_rib_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_parsec_rib_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_parsec_rib_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
