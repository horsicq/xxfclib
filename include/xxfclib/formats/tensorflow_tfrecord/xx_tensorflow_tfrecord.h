/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.tensorflow.org/tutorials/load_data/tfrecord */
#ifndef XX_TENSORFLOW_TFRECORD_H
#define XX_TENSORFLOW_TFRECORD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tensorflow_tfrecord {
    Abstractformat format;
} xx_tensorflow_tfrecord;
XXFC_API void xx_tensorflow_tfrecord_init(xx_tensorflow_tfrecord *, xx_io_device *, int64_t);
XXFC_API xx_tensorflow_tfrecord *xx_tensorflow_tfrecord_create(xx_io_device *, int64_t);
XXFC_API void xx_tensorflow_tfrecord_destroy(xx_tensorflow_tfrecord *);
XXFC_API void xx_tensorflow_tfrecord_free(xx_tensorflow_tfrecord *);
XXFC_API bool xx_tensorflow_tfrecord_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_tensorflow_tfrecord_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_tensorflow_tfrecord_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_tensorflow_tfrecord_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_tensorflow_tfrecord_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_tensorflow_tfrecord_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_tensorflow_tfrecord_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
