/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/obspy/obspy/blob/master/obspy/io/seg2/seg2.py */
#ifndef XX_SEISMIC_SEG2_H
#define XX_SEISMIC_SEG2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_seismic_seg2 {
    Abstractformat format;
} xx_seismic_seg2;
XXFC_API void xx_seismic_seg2_init(xx_seismic_seg2 *, xx_io_device *, int64_t);
XXFC_API xx_seismic_seg2 *xx_seismic_seg2_create(xx_io_device *, int64_t);
XXFC_API void xx_seismic_seg2_destroy(xx_seismic_seg2 *);
XXFC_API void xx_seismic_seg2_free(xx_seismic_seg2 *);
XXFC_API bool xx_seismic_seg2_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_seismic_seg2_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_seismic_seg2_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_seismic_seg2_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_seismic_seg2_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_seismic_seg2_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_seismic_seg2_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
