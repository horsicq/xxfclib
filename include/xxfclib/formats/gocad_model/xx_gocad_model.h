/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/lanl/LaGriT/master/src/read_gocad_tsurf.f
 * GOCAD TSurf1 triangulated surfaces: complete header/property declarations and TFACE records with unique local vertex IDs, finite coordinates/properties, resolved ATOM/TRGL/border references and END framing. Original descriptor and typed surface records exported; external coordinate systems/solid/voxel/complex extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_GOCAD_MODEL_H
#define XX_GOCAD_MODEL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gocad_model {Abstractformat format;} xx_gocad_model;
XXFC_API void xx_gocad_model_init(xx_gocad_model *,xx_io_device *,int64_t);
XXFC_API xx_gocad_model *xx_gocad_model_create(xx_io_device *,int64_t);
XXFC_API void xx_gocad_model_destroy(xx_gocad_model *);
XXFC_API void xx_gocad_model_free(xx_gocad_model *);
XXFC_API bool xx_gocad_model_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gocad_model_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
