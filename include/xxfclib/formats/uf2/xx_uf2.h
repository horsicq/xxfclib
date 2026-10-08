/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://github.com/microsoft/uf2/blob/master/README.md
 * Publishes stored payload components; see docs/registered_second_fifty_formats.md.
 */
#ifndef XX_UF2_H
#define XX_UF2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_uf2 { Abstractformat format; } xx_uf2;
XXFC_API void xx_uf2_init(xx_uf2 *,xx_io_device *,int64_t);
XXFC_API xx_uf2 *xx_uf2_create(xx_io_device *,int64_t);
XXFC_API void xx_uf2_destroy(xx_uf2 *);
XXFC_API void xx_uf2_free(xx_uf2 *);
XXFC_API bool xx_uf2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_uf2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_uf2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_uf2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_uf2_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
