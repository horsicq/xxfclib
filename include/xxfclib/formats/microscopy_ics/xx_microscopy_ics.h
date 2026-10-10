/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/svi-opensource/libics */
#ifndef XX_MICROSCOPY_ICS_H
#define XX_MICROSCOPY_ICS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_microscopy_ics {
    Abstractformat format;
} xx_microscopy_ics;
XXFC_API void xx_microscopy_ics_init(xx_microscopy_ics *, xx_io_device *, int64_t);
XXFC_API xx_microscopy_ics *xx_microscopy_ics_create(xx_io_device *, int64_t);
XXFC_API void xx_microscopy_ics_destroy(xx_microscopy_ics *);
XXFC_API void xx_microscopy_ics_free(xx_microscopy_ics *);
XXFC_API bool xx_microscopy_ics_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_microscopy_ics_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_microscopy_ics_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_microscopy_ics_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_microscopy_ics_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_microscopy_ics_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_microscopy_ics_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
