/* SPDX-License-Identifier: MIT
 * Primary reference: https://fontforge.org/docs/techref/sfdformat.html
 * FontForge SFD1.0 outline subset: typed font metrics and complete counted glyphs with checked encodings, finite move/line/cubic contours and balanced spline/character/font terminators. Fore opens each legacy outline; one immediate optional SplineSet token is accepted. Original descriptor and glyph programs exported; bitmap/layer/reference/kerning/lookup extensions declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_FONTFORGE_SFD_H
#define XX_FONTFORGE_SFD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_fontforge_sfd {Abstractformat format;} xx_fontforge_sfd;
XXFC_API void xx_fontforge_sfd_init(xx_fontforge_sfd *,xx_io_device *,int64_t);
XXFC_API xx_fontforge_sfd *xx_fontforge_sfd_create(xx_io_device *,int64_t);
XXFC_API void xx_fontforge_sfd_destroy(xx_fontforge_sfd *);
XXFC_API void xx_fontforge_sfd_free(xx_fontforge_sfd *);
XXFC_API bool xx_fontforge_sfd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_fontforge_sfd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_fontforge_sfd_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_fontforge_sfd_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_fontforge_sfd_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
