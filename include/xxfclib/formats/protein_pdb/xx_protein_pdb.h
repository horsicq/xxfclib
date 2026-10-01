/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.wwpdb.org/documentation/file-format-content/format33/sect9.html */
#ifndef XX_PROTEIN_PDB_H
#define XX_PROTEIN_PDB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_protein_pdb { Abstractformat format; } xx_protein_pdb;
XXFC_API void xx_protein_pdb_init(xx_protein_pdb *,xx_io_device *,int64_t);
XXFC_API xx_protein_pdb *xx_protein_pdb_create(xx_io_device *,int64_t);
XXFC_API void xx_protein_pdb_destroy(xx_protein_pdb *);
XXFC_API void xx_protein_pdb_free(xx_protein_pdb *);
XXFC_API bool xx_protein_pdb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_protein_pdb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
