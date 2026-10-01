/* SPDX-License-Identifier: MIT
 * Primary reference: https://opengex.org/opengex-spec.pdf
 * OpenGEX static geometry subset: typed metrics, object/material/node identifiers and resolved references, finite transforms and vertex arrays, bounded triangle indexes and matching attribute cardinalities. Original top-level scene structures exported; animation/skinning/morphing/custom extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_OPENGEX_MODEL_H
#define XX_OPENGEX_MODEL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_opengex_model {Abstractformat format;} xx_opengex_model;
XXFC_API void xx_opengex_model_init(xx_opengex_model *,xx_io_device *,int64_t);
XXFC_API xx_opengex_model *xx_opengex_model_create(xx_io_device *,int64_t);
XXFC_API void xx_opengex_model_destroy(xx_opengex_model *);
XXFC_API void xx_opengex_model_free(xx_opengex_model *);
XXFC_API bool xx_opengex_model_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_opengex_model_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
