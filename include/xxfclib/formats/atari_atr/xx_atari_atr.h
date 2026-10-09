/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_ATARI_ATR_H
#define XX_ATARI_ATR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_atari_atr { Abstractformat format; } xx_atari_atr;
XXFC_API void xx_atari_atr_init(xx_atari_atr *,xx_io_device *,int64_t);
XXFC_API xx_atari_atr *xx_atari_atr_create(xx_io_device *,int64_t);
XXFC_API void xx_atari_atr_destroy(xx_atari_atr *);
XXFC_API void xx_atari_atr_free(xx_atari_atr *);
XXFC_API bool xx_atari_atr_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_atari_atr_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_atari_atr_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_atari_atr_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_atari_atr_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_atari_atr_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_atari_atr_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
