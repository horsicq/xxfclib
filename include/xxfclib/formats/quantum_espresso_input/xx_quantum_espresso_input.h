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
#endif
