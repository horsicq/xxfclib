/* SPDX-License-Identifier: MIT
 * Wire specification: https://ase-lib.org/_modules/ase/io/xyz.html */
#ifndef XX_MOLECULE_XYZ_H
#define XX_MOLECULE_XYZ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_molecule_xyz { Abstractformat format; } xx_molecule_xyz;
XXFC_API void xx_molecule_xyz_init(xx_molecule_xyz *,xx_io_device *,int64_t);
XXFC_API xx_molecule_xyz *xx_molecule_xyz_create(xx_io_device *,int64_t);
XXFC_API void xx_molecule_xyz_destroy(xx_molecule_xyz *);
XXFC_API void xx_molecule_xyz_free(xx_molecule_xyz *);
XXFC_API bool xx_molecule_xyz_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_molecule_xyz_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
