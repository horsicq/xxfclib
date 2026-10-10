/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_SONY_VAB_H
#define XX_SONY_VAB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sony_vab {
    Abstractformat format;
} xx_sony_vab;
XXFC_API void xx_sony_vab_init(xx_sony_vab *, xx_io_device *, int64_t);
XXFC_API xx_sony_vab *xx_sony_vab_create(xx_io_device *, int64_t);
XXFC_API void xx_sony_vab_destroy(xx_sony_vab *);
XXFC_API void xx_sony_vab_free(xx_sony_vab *);
XXFC_API bool xx_sony_vab_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sony_vab_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sony_vab_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sony_vab_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sony_vab_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sony_vab_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sony_vab_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
