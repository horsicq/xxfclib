/* SPDX-License-Identifier: MIT. Bounded native ea_exa reader. */
#ifndef XX_EA_EXA_H
#define XX_EA_EXA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ea_exa {
    Abstractformat format;
} xx_ea_exa;
XXFC_API void xx_ea_exa_init(xx_ea_exa *, xx_io_device *, int64_t);
XXFC_API xx_ea_exa *xx_ea_exa_create(xx_io_device *, int64_t);
XXFC_API void xx_ea_exa_destroy(xx_ea_exa *);
XXFC_API void xx_ea_exa_free(xx_ea_exa *);
XXFC_API bool xx_ea_exa_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_ea_exa_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ea_exa_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_ea_exa_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ea_exa_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ea_exa_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_ea_exa_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
