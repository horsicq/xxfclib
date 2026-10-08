/* SPDX-License-Identifier: MIT
 * Primary reference: https://tug.ctan.org/info/knuth-pdf/etc/vftovp.pdf
 * TeX VF202: complete preamble/font definitions/short and long character packets/postamble, unique font and glyph IDs, finite positive design size, bounded valid DVI packet commands with balanced stack and resolved font selections. Original encoded font/character programs exported; no DVI replay or font loading.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_TEX_VF_H
#define XX_TEX_VF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tex_vf {Abstractformat format;} xx_tex_vf;
XXFC_API void xx_tex_vf_init(xx_tex_vf *,xx_io_device *,int64_t);
XXFC_API xx_tex_vf *xx_tex_vf_create(xx_io_device *,int64_t);
XXFC_API void xx_tex_vf_destroy(xx_tex_vf *);
XXFC_API void xx_tex_vf_free(xx_tex_vf *);
XXFC_API bool xx_tex_vf_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tex_vf_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tex_vf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tex_vf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tex_vf_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
