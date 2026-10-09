/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/ipl.c
 * Scanalytics IPLab100f image stacks: complete endian marker/version/data/fini framing, counted channel/Z/T planes and checked integer/finite floating samples. Original descriptor and typed planes exported; optional following tags/unknown sample types declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_SCANALYTICS_IPLAB_H
#define XX_SCANALYTICS_IPLAB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_scanalytics_iplab {Abstractformat format;} xx_scanalytics_iplab;
XXFC_API void xx_scanalytics_iplab_init(xx_scanalytics_iplab *,xx_io_device *,int64_t);
XXFC_API xx_scanalytics_iplab *xx_scanalytics_iplab_create(xx_io_device *,int64_t);
XXFC_API void xx_scanalytics_iplab_destroy(xx_scanalytics_iplab *);
XXFC_API void xx_scanalytics_iplab_free(xx_scanalytics_iplab *);
XXFC_API bool xx_scanalytics_iplab_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_scanalytics_iplab_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_scanalytics_iplab_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_scanalytics_iplab_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_scanalytics_iplab_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_scanalytics_iplab_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_scanalytics_iplab_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
