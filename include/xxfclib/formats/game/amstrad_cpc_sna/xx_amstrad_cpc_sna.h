/* SPDX-License-Identifier: MIT. Bounded original-component reader. */
#ifndef XX_AMSTRAD_CPC_SNA_H
#define XX_AMSTRAD_CPC_SNA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amstrad_cpc_sna {
    Abstractformat format;
} xx_amstrad_cpc_sna;
XXFC_API void xx_amstrad_cpc_sna_init(xx_amstrad_cpc_sna *, xx_io_device *, int64_t);
XXFC_API xx_amstrad_cpc_sna *xx_amstrad_cpc_sna_create(xx_io_device *, int64_t);
XXFC_API void xx_amstrad_cpc_sna_destroy(xx_amstrad_cpc_sna *);
XXFC_API void xx_amstrad_cpc_sna_free(xx_amstrad_cpc_sna *);
XXFC_API bool xx_amstrad_cpc_sna_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_amstrad_cpc_sna_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_amstrad_cpc_sna_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_amstrad_cpc_sna_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_amstrad_cpc_sna_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_amstrad_cpc_sna_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_amstrad_cpc_sna_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
