/* SPDX-License-Identifier: MIT
 * Primary reference: https://docs.software.vt.edu/abaqusv2025/English/SIMACAEMODRefMap/simamod-c-inputsyntax.htm
 * Abaqus mesh input subset: complete typed NODE/ELEMENT/ELSET/NSET/SURFACE blocks, finite coordinates, unique local identifiers and resolved connectivity/set references. Original typed mesh blocks and comments exported; includes, solver steps, material evaluation and arbitrary keywords declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_ABAQUS_INPUT_H
#define XX_ABAQUS_INPUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_abaqus_input {Abstractformat format;} xx_abaqus_input;
XXFC_API void xx_abaqus_input_init(xx_abaqus_input *,xx_io_device *,int64_t);
XXFC_API xx_abaqus_input *xx_abaqus_input_create(xx_io_device *,int64_t);
XXFC_API void xx_abaqus_input_destroy(xx_abaqus_input *);
XXFC_API void xx_abaqus_input_free(xx_abaqus_input *);
XXFC_API bool xx_abaqus_input_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_abaqus_input_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
