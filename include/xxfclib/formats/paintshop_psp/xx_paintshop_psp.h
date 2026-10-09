/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/GNOME/gimp/master/plug-ins/common/file-psp.c
 * Native PSP3.0 RGB24/grayscale8 raster images with one color bitmap per layer: exact38-byte image,375-byte layer and12-byte channel descriptors, bounded rectangles/counts, stored or PSP RLE plane framing. Original encoded headers/channels exported; creator/color-table/alpha/user-mask blocks, other versions/compression and rendering declined. Primary original unavailable; independently specified wire controls only.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_PAINTSHOP_PSP_H
#define XX_PAINTSHOP_PSP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_paintshop_psp {Abstractformat format;} xx_paintshop_psp;
XXFC_API void xx_paintshop_psp_init(xx_paintshop_psp *,xx_io_device *,int64_t);
XXFC_API xx_paintshop_psp *xx_paintshop_psp_create(xx_io_device *,int64_t);
XXFC_API void xx_paintshop_psp_destroy(xx_paintshop_psp *);
XXFC_API void xx_paintshop_psp_free(xx_paintshop_psp *);
XXFC_API bool xx_paintshop_psp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_paintshop_psp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_paintshop_psp_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_paintshop_psp_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_paintshop_psp_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_paintshop_psp_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_paintshop_psp_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
