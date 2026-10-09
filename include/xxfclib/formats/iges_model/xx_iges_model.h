/* SPDX-License-Identifier: MIT
 * Primary reference: https://github.com/pyvista/pyiges
 * IGES5.x ASCII CAD subset: complete80-column S/G/D/P/T sequence/count framing, bounded Hollerith/numeric parameter grammar, typed supported entities and resolved local directory references; original descriptor/directory/entity records exported; unsupported entity forms and external references declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_IGES_MODEL_H
#define XX_IGES_MODEL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_iges_model {Abstractformat format;} xx_iges_model;
XXFC_API void xx_iges_model_init(xx_iges_model *,xx_io_device *,int64_t);
XXFC_API xx_iges_model *xx_iges_model_create(xx_io_device *,int64_t);
XXFC_API void xx_iges_model_destroy(xx_iges_model *);
XXFC_API void xx_iges_model_free(xx_iges_model *);
XXFC_API bool xx_iges_model_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_iges_model_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_iges_model_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_iges_model_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_iges_model_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_iges_model_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_iges_model_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
