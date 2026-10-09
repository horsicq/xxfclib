/* SPDX-License-Identifier: MIT. Bounded native adobe_director_cxt reader. */
#ifndef XX_ADOBE_DIRECTOR_CXT_H
#define XX_ADOBE_DIRECTOR_CXT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_adobe_director_cxt { Abstractformat format; } xx_adobe_director_cxt;
XXFC_API void xx_adobe_director_cxt_init(xx_adobe_director_cxt *,xx_io_device *,int64_t);
XXFC_API xx_adobe_director_cxt *xx_adobe_director_cxt_create(xx_io_device *,int64_t);
XXFC_API void xx_adobe_director_cxt_destroy(xx_adobe_director_cxt *);
XXFC_API void xx_adobe_director_cxt_free(xx_adobe_director_cxt *);
XXFC_API bool xx_adobe_director_cxt_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_adobe_director_cxt_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_adobe_director_cxt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_adobe_director_cxt_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_adobe_director_cxt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_adobe_director_cxt_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_adobe_director_cxt_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
