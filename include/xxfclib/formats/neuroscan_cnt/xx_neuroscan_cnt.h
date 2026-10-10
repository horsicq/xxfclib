/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/mne-tools/mne-python/blob/main/mne/io/cnt/cnt.py */
#ifndef XX_NEUROSCAN_CNT_H
#define XX_NEUROSCAN_CNT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_neuroscan_cnt {
    Abstractformat format;
} xx_neuroscan_cnt;
XXFC_API void xx_neuroscan_cnt_init(xx_neuroscan_cnt *, xx_io_device *, int64_t);
XXFC_API xx_neuroscan_cnt *xx_neuroscan_cnt_create(xx_io_device *, int64_t);
XXFC_API void xx_neuroscan_cnt_destroy(xx_neuroscan_cnt *);
XXFC_API void xx_neuroscan_cnt_free(xx_neuroscan_cnt *);
XXFC_API bool xx_neuroscan_cnt_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_neuroscan_cnt_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_neuroscan_cnt_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_neuroscan_cnt_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_neuroscan_cnt_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_neuroscan_cnt_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_neuroscan_cnt_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
