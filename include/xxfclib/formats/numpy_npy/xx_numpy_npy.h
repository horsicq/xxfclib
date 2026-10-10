/* SPDX-License-Identifier: MIT
 * Independently implemented from https://numpy.org/doc/stable/reference/generated/numpy.lib.format.html
 * Bounded encoded-component extraction. */
#ifndef XX_NUMPY_NPY_H
#define XX_NUMPY_NPY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_numpy_npy {
    Abstractformat format;
} xx_numpy_npy;
XXFC_API void xx_numpy_npy_init(xx_numpy_npy *, xx_io_device *, int64_t);
XXFC_API xx_numpy_npy *xx_numpy_npy_create(xx_io_device *, int64_t);
XXFC_API void xx_numpy_npy_destroy(xx_numpy_npy *);
XXFC_API void xx_numpy_npy_free(xx_numpy_npy *);
XXFC_API bool xx_numpy_npy_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_numpy_npy_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_numpy_npy_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_numpy_npy_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_numpy_npy_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_numpy_npy_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_numpy_npy_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
