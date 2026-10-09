/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/Ericsson/ETCPACK/master/source/etcpack.cxx
 * PKM1 ETC1 and PKM2 ETC2/EAC formats0,1,3-11, canonical four-pixel rounded geometry and exact encoded block counts. Deprecated type2, mipmaps and texture decoding are unsupported.
 * Limit64MiB,4096 components. No payload or external resource is executed.
 */
#ifndef XX_PKM_TEXTURE_H
#define XX_PKM_TEXTURE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_pkm_texture { Abstractformat format; } xx_pkm_texture;
XXFC_API void xx_pkm_texture_init(xx_pkm_texture *,xx_io_device *,int64_t);
XXFC_API xx_pkm_texture *xx_pkm_texture_create(xx_io_device *,int64_t);
XXFC_API void xx_pkm_texture_destroy(xx_pkm_texture *);
XXFC_API void xx_pkm_texture_free(xx_pkm_texture *);
XXFC_API bool xx_pkm_texture_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_pkm_texture_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_pkm_texture_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_pkm_texture_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_pkm_texture_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_pkm_texture_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_pkm_texture_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
