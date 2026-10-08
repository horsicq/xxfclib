/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/openbabel/openbabel/blob/master/src/formats/mdlformat.cpp */
#ifndef XX_MDL_MOLFILE_H
#define XX_MDL_MOLFILE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mdl_molfile { Abstractformat format; } xx_mdl_molfile;
XXFC_API void xx_mdl_molfile_init(xx_mdl_molfile *,xx_io_device *,int64_t);
XXFC_API xx_mdl_molfile *xx_mdl_molfile_create(xx_io_device *,int64_t);
XXFC_API void xx_mdl_molfile_destroy(xx_mdl_molfile *);
XXFC_API void xx_mdl_molfile_free(xx_mdl_molfile *);
XXFC_API bool xx_mdl_molfile_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mdl_molfile_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_mdl_molfile_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_mdl_molfile_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_mdl_molfile_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
