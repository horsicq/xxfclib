/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.quantum-espresso.org/Doc/INPUT_PW.html */
#ifndef XX_QUANTUM_ESPRESSO_INPUT_H
#define XX_QUANTUM_ESPRESSO_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_quantum_espresso_input { Abstractformat format; } xx_quantum_espresso_input;
XXFC_API void xx_quantum_espresso_input_init(xx_quantum_espresso_input *,xx_io_device *,int64_t);
XXFC_API xx_quantum_espresso_input *xx_quantum_espresso_input_create(xx_io_device *,int64_t);
XXFC_API void xx_quantum_espresso_input_destroy(xx_quantum_espresso_input *);
XXFC_API void xx_quantum_espresso_input_free(xx_quantum_espresso_input *);
XXFC_API bool xx_quantum_espresso_input_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_quantum_espresso_input_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_quantum_espresso_input_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_quantum_espresso_input_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_quantum_espresso_input_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_quantum_espresso_input_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_quantum_espresso_input_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
