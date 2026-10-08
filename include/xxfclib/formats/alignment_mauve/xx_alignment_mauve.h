/* SPDX-License-Identifier: MIT
 * Wire specification: https://darlinglab.org/mauve/user-guide/files.html */
#ifndef XX_ALIGNMENT_MAUVE_H
#define XX_ALIGNMENT_MAUVE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_alignment_mauve { Abstractformat format; } xx_alignment_mauve;
XXFC_API void xx_alignment_mauve_init(xx_alignment_mauve *,xx_io_device *,int64_t);
XXFC_API xx_alignment_mauve *xx_alignment_mauve_create(xx_io_device *,int64_t);
XXFC_API void xx_alignment_mauve_destroy(xx_alignment_mauve *);
XXFC_API void xx_alignment_mauve_free(xx_alignment_mauve *);
XXFC_API bool xx_alignment_mauve_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_alignment_mauve_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_alignment_mauve_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_alignment_mauve_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_alignment_mauve_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
