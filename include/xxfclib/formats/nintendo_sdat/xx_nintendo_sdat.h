/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_NINTENDO_SDAT_H
#define XX_NINTENDO_SDAT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_sdat { Abstractformat format; } xx_nintendo_sdat;
XXFC_API void xx_nintendo_sdat_init(xx_nintendo_sdat *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_sdat *xx_nintendo_sdat_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_sdat_destroy(xx_nintendo_sdat *);
XXFC_API void xx_nintendo_sdat_free(xx_nintendo_sdat *);
XXFC_API bool xx_nintendo_sdat_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_sdat_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_sdat_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_sdat_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_sdat_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_sdat_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_sdat_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
