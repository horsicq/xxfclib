/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_AMIGA_IPF_H
#define XX_AMIGA_IPF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amiga_ipf {
    Abstractformat format;
} xx_amiga_ipf;
XXFC_API void xx_amiga_ipf_init(xx_amiga_ipf *, xx_io_device *, int64_t);
XXFC_API xx_amiga_ipf *xx_amiga_ipf_create(xx_io_device *, int64_t);
XXFC_API void xx_amiga_ipf_destroy(xx_amiga_ipf *);
XXFC_API void xx_amiga_ipf_free(xx_amiga_ipf *);
XXFC_API bool xx_amiga_ipf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_amiga_ipf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_amiga_ipf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_amiga_ipf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_amiga_ipf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_amiga_ipf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_amiga_ipf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
