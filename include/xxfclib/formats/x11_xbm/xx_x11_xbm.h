/* SPDX-License-Identifier: MIT
 * Primary reference: https://www.x.org/releases/X11R7.7/doc/libX11/libX11/libX11.html
 * X11 XBM: strict bounded ASCII width/height and optional paired hotspots, one unsigned char/char bitmap initializer with exact hex-byte count. Decoded LSB-first row bytes plus original descriptor exported; no C execution. X10 short arrays and additional C declarations declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_X11_XBM_H
#define XX_X11_XBM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_x11_xbm {Abstractformat format;} xx_x11_xbm;
XXFC_API void xx_x11_xbm_init(xx_x11_xbm *,xx_io_device *,int64_t);
XXFC_API xx_x11_xbm *xx_x11_xbm_create(xx_io_device *,int64_t);
XXFC_API void xx_x11_xbm_destroy(xx_x11_xbm *);
XXFC_API void xx_x11_xbm_free(xx_x11_xbm *);
XXFC_API bool xx_x11_xbm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_x11_xbm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_x11_xbm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_x11_xbm_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_x11_xbm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_x11_xbm_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_x11_xbm_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
