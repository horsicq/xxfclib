/* SPDX-License-Identifier: MIT
 * Primary reference: https://openusd.org/release/spec_usda.html
 * OpenUSD USDA1.0 static Mesh/Xform subset: complete bounded typed layer/prim/property grammar, unique prim paths/settings, finite point/transform arrays and counted
 * resolved face topology/local references; original metadata/prim/property records exported; variants/composition/payloads/animation/unknown schemas declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_OPENUSD_USDA_H
#define XX_OPENUSD_USDA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_openusd_usda {
    Abstractformat format;
} xx_openusd_usda;
XXFC_API void xx_openusd_usda_init(xx_openusd_usda *, xx_io_device *, int64_t);
XXFC_API xx_openusd_usda *xx_openusd_usda_create(xx_io_device *, int64_t);
XXFC_API void xx_openusd_usda_destroy(xx_openusd_usda *);
XXFC_API void xx_openusd_usda_free(xx_openusd_usda *);
XXFC_API bool xx_openusd_usda_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_openusd_usda_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_openusd_usda_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_openusd_usda_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_openusd_usda_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_openusd_usda_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_openusd_usda_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
