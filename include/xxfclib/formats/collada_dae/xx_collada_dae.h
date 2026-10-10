/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.khronos.org/files/collada_spec_1_4.pdf
 * COLLADA1.4.1 static geometry/scene subset: complete bounded XML grammar, counted float arrays/accessors/indexed triangles, local identifiers/references and finite
 * scene transforms. Original asset/library/scene structures exported; effects/animation/cameras/lights/controllers/custom extensions, XML entities/comments/CDATA/DOCTYPE
 * and non-UTF8 encodings declined. ASCII NCName identifiers, calendar-valid ISO timestamps and XML1.0 declaration are checked. Bounded32MiB input,4096 components and
 * bounded work.
 */
#ifndef XX_COLLADA_DAE_H
#define XX_COLLADA_DAE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_collada_dae {
    Abstractformat format;
} xx_collada_dae;
XXFC_API void xx_collada_dae_init(xx_collada_dae *, xx_io_device *, int64_t);
XXFC_API xx_collada_dae *xx_collada_dae_create(xx_io_device *, int64_t);
XXFC_API void xx_collada_dae_destroy(xx_collada_dae *);
XXFC_API void xx_collada_dae_free(xx_collada_dae *);
XXFC_API bool xx_collada_dae_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_collada_dae_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_collada_dae_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_collada_dae_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_collada_dae_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_collada_dae_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_collada_dae_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
