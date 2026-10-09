/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_IFF_8SVX_H
#define XX_IFF_8SVX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_iff_8svx { Abstractformat format; } xx_iff_8svx;
XXFC_API void xx_iff_8svx_init(xx_iff_8svx *,xx_io_device *,int64_t);
XXFC_API xx_iff_8svx *xx_iff_8svx_create(xx_io_device *,int64_t);
XXFC_API void xx_iff_8svx_destroy(xx_iff_8svx *);
XXFC_API void xx_iff_8svx_free(xx_iff_8svx *);
XXFC_API bool xx_iff_8svx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_iff_8svx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_iff_8svx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_iff_8svx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_iff_8svx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_iff_8svx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_iff_8svx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
