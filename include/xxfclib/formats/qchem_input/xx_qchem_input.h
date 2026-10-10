/* SPDX-License-Identifier: MIT
 * Primary reference: https://manual.q-chem.com/latest/Ch3.S3.html
 * Q-Chem molecule/rem input subset: balanced complete comment/rem/molecule sections, unique recognized settings, checked charge/multiplicity and finite element
 * coordinates for H through Xe and consistent charge/spin electron parity. Original settings/molecule sections exported; includes, basis/ECP blocks, fragments, variables
 * and job chains declined. Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_QCHEM_INPUT_H
#define XX_QCHEM_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_qchem_input {
    Abstractformat format;
} xx_qchem_input;
XXFC_API void xx_qchem_input_init(xx_qchem_input *, xx_io_device *, int64_t);
XXFC_API xx_qchem_input *xx_qchem_input_create(xx_io_device *, int64_t);
XXFC_API void xx_qchem_input_destroy(xx_qchem_input *);
XXFC_API void xx_qchem_input_free(xx_qchem_input *);
XXFC_API bool xx_qchem_input_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_qchem_input_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_qchem_input_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_qchem_input_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_qchem_input_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_qchem_input_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_qchem_input_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
