/* SPDX-License-Identifier: MIT
 * Wire specification: https://genome.ucsc.edu/FAQ/FAQformat.html */
#ifndef XX_UCSC_NIB_H
#define XX_UCSC_NIB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ucsc_nib { Abstractformat format; } xx_ucsc_nib;
XXFC_API void xx_ucsc_nib_init(xx_ucsc_nib *,xx_io_device *,int64_t);
XXFC_API xx_ucsc_nib *xx_ucsc_nib_create(xx_io_device *,int64_t);
XXFC_API void xx_ucsc_nib_destroy(xx_ucsc_nib *);
XXFC_API void xx_ucsc_nib_free(xx_ucsc_nib *);
XXFC_API bool xx_ucsc_nib_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ucsc_nib_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ucsc_nib_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ucsc_nib_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ucsc_nib_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ucsc_nib_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ucsc_nib_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
