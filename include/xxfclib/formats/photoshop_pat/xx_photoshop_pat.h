/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/SethRobinson/Patchy/main/src/psd/pat_reader.cpp
 * Standalone Photoshop 8BPT PAT1 RGB/grayscale sampled patterns: complete count and UTF16BE names/IDs, VMA3 slots and exact stored/row-PackBits8-bit plane extents. Original encoded pattern records exported; optional typed empty8BIMphry hierarchy accepted. Nonempty hierarchies, other modes/depths/compression and pixel rendering declined.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_PHOTOSHOP_PAT_H
#define XX_PHOTOSHOP_PAT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_photoshop_pat {Abstractformat format;} xx_photoshop_pat;
XXFC_API void xx_photoshop_pat_init(xx_photoshop_pat *,xx_io_device *,int64_t);
XXFC_API xx_photoshop_pat *xx_photoshop_pat_create(xx_io_device *,int64_t);
XXFC_API void xx_photoshop_pat_destroy(xx_photoshop_pat *);
XXFC_API void xx_photoshop_pat_free(xx_photoshop_pat *);
XXFC_API bool xx_photoshop_pat_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_photoshop_pat_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_photoshop_pat_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_photoshop_pat_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_photoshop_pat_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_photoshop_pat_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_photoshop_pat_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
