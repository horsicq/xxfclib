/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_ZX_HOBETA_H
#define XX_ZX_HOBETA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_zx_hobeta {
    Abstractformat format;
} xx_zx_hobeta;
XXFC_API void xx_zx_hobeta_init(xx_zx_hobeta *, xx_io_device *, int64_t);
XXFC_API xx_zx_hobeta *xx_zx_hobeta_create(xx_io_device *, int64_t);
XXFC_API void xx_zx_hobeta_destroy(xx_zx_hobeta *);
XXFC_API void xx_zx_hobeta_free(xx_zx_hobeta *);
XXFC_API bool xx_zx_hobeta_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_zx_hobeta_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_zx_hobeta_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_zx_hobeta_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_zx_hobeta_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_zx_hobeta_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_zx_hobeta_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
