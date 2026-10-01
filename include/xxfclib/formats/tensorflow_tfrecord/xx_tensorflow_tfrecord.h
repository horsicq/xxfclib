/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.tensorflow.org/tutorials/load_data/tfrecord */
#ifndef XX_TENSORFLOW_TFRECORD_H
#define XX_TENSORFLOW_TFRECORD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tensorflow_tfrecord { Abstractformat format; } xx_tensorflow_tfrecord;
XXFC_API void xx_tensorflow_tfrecord_init(xx_tensorflow_tfrecord *,xx_io_device *,int64_t);
XXFC_API xx_tensorflow_tfrecord *xx_tensorflow_tfrecord_create(xx_io_device *,int64_t);
XXFC_API void xx_tensorflow_tfrecord_destroy(xx_tensorflow_tfrecord *);
XXFC_API void xx_tensorflow_tfrecord_free(xx_tensorflow_tfrecord *);
XXFC_API bool xx_tensorflow_tfrecord_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tensorflow_tfrecord_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
