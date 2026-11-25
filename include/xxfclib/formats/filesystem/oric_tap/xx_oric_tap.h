/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_ORIC_TAP_H
#define XX_ORIC_TAP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_oric_tap {
    Abstractformat format;
} xx_oric_tap;
XXFC_API void xx_oric_tap_init(xx_oric_tap *, xx_io_device *, int64_t);
XXFC_API xx_oric_tap *xx_oric_tap_create(xx_io_device *, int64_t);
XXFC_API void xx_oric_tap_destroy(xx_oric_tap *);
XXFC_API void xx_oric_tap_free(xx_oric_tap *);
XXFC_API bool xx_oric_tap_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_oric_tap_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_oric_tap_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_oric_tap_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_oric_tap_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_oric_tap_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_oric_tap_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
