/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/python/cpython/blob/3.13/Lib/pickletools.py */
#ifndef XX_PYTHON_PICKLE_H
#define XX_PYTHON_PICKLE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_python_pickle {
    Abstractformat format;
} xx_python_pickle;
XXFC_API void xx_python_pickle_init(xx_python_pickle *, xx_io_device *, int64_t);
XXFC_API xx_python_pickle *xx_python_pickle_create(xx_io_device *, int64_t);
XXFC_API void xx_python_pickle_destroy(xx_python_pickle *);
XXFC_API void xx_python_pickle_free(xx_python_pickle *);
XXFC_API bool xx_python_pickle_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_python_pickle_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_python_pickle_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_python_pickle_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_python_pickle_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_python_pickle_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_python_pickle_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
