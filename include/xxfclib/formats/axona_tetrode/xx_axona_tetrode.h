/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/NeuralEnsemble/python-neo/blob/master/neo/rawio/axonarawio.py */
#ifndef XX_AXONA_TETRODE_H
#define XX_AXONA_TETRODE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_axona_tetrode { Abstractformat format; } xx_axona_tetrode;
XXFC_API void xx_axona_tetrode_init(xx_axona_tetrode *,xx_io_device *,int64_t);
XXFC_API xx_axona_tetrode *xx_axona_tetrode_create(xx_io_device *,int64_t);
XXFC_API void xx_axona_tetrode_destroy(xx_axona_tetrode *);
XXFC_API void xx_axona_tetrode_free(xx_axona_tetrode *);
XXFC_API bool xx_axona_tetrode_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_axona_tetrode_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_axona_tetrode_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_axona_tetrode_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_axona_tetrode_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_axona_tetrode_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_axona_tetrode_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
