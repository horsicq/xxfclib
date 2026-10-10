/* SPDX-License-Identifier: MIT
 * Independently implemented from https://docs.hdfgroup.org/archive/support/ftp/HDF/prev-Documentation/HDF4r15_SpecDG.pdf
 * Bounded encoded-component extraction. */
#ifndef XX_HDF4_H
#define XX_HDF4_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hdf4 {
    Abstractformat format;
} xx_hdf4;
XXFC_API void xx_hdf4_init(xx_hdf4 *, xx_io_device *, int64_t);
XXFC_API xx_hdf4 *xx_hdf4_create(xx_io_device *, int64_t);
XXFC_API void xx_hdf4_destroy(xx_hdf4 *);
XXFC_API void xx_hdf4_free(xx_hdf4 *);
XXFC_API bool xx_hdf4_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_hdf4_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_hdf4_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_hdf4_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_hdf4_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_hdf4_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_hdf4_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
