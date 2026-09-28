/* SPDX-License-Identifier: MIT
 * Wire specification: https://gfa-spec.github.io/GFA-spec/GFA1.html */
#ifndef XX_ASSEMBLY_GFA_H
#define XX_ASSEMBLY_GFA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_assembly_gfa { Abstractformat format; } xx_assembly_gfa;
XXFC_API void xx_assembly_gfa_init(xx_assembly_gfa *,xx_io_device *,int64_t);
XXFC_API xx_assembly_gfa *xx_assembly_gfa_create(xx_io_device *,int64_t);
XXFC_API void xx_assembly_gfa_destroy(xx_assembly_gfa *);
XXFC_API void xx_assembly_gfa_free(xx_assembly_gfa *);
XXFC_API bool xx_assembly_gfa_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_assembly_gfa_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
