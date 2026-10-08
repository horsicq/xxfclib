/* SPDX-License-Identifier: MIT. Bounded A2R2 STRM and A2R3 RWCP/SLVD reader.
 * Lists/extracts capture and solved-track descriptors, index timestamps and
 * raw flux/bit bytes; does not decode a sector disk or expand mirrored tracks.
 */
#ifndef XX_APPLE_A2R_H
#define XX_APPLE_A2R_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_apple_a2r { Abstractformat format; } xx_apple_a2r;
XXFC_API void xx_apple_a2r_init(xx_apple_a2r *,xx_io_device *,int64_t);
XXFC_API xx_apple_a2r *xx_apple_a2r_create(xx_io_device *,int64_t);
XXFC_API void xx_apple_a2r_destroy(xx_apple_a2r *);
XXFC_API void xx_apple_a2r_free(xx_apple_a2r *);
XXFC_API bool xx_apple_a2r_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_apple_a2r_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_apple_a2r_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_apple_a2r_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_apple_a2r_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
