/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.inivation.com/software/software-advanced-usage/file-formats/aedat-2.0.html */
#ifndef XX_INIVATION_AEDAT_H
#define XX_INIVATION_AEDAT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_inivation_aedat {
    Abstractformat format;
} xx_inivation_aedat;
XXFC_API void xx_inivation_aedat_init(xx_inivation_aedat *, xx_io_device *, int64_t);
XXFC_API xx_inivation_aedat *xx_inivation_aedat_create(xx_io_device *, int64_t);
XXFC_API void xx_inivation_aedat_destroy(xx_inivation_aedat *);
XXFC_API void xx_inivation_aedat_free(xx_inivation_aedat *);
XXFC_API bool xx_inivation_aedat_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_inivation_aedat_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_inivation_aedat_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_inivation_aedat_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_inivation_aedat_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_inivation_aedat_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_inivation_aedat_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
