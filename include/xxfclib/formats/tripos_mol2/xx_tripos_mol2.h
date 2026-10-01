/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/openbabel/openbabel/blob/master/src/formats/mol2format.cpp */
#ifndef XX_TRIPOS_MOL2_H
#define XX_TRIPOS_MOL2_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_tripos_mol2 { Abstractformat format; } xx_tripos_mol2;
XXFC_API void xx_tripos_mol2_init(xx_tripos_mol2 *,xx_io_device *,int64_t);
XXFC_API xx_tripos_mol2 *xx_tripos_mol2_create(xx_io_device *,int64_t);
XXFC_API void xx_tripos_mol2_destroy(xx_tripos_mol2 *);
XXFC_API void xx_tripos_mol2_free(xx_tripos_mol2 *);
XXFC_API bool xx_tripos_mol2_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_tripos_mol2_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
