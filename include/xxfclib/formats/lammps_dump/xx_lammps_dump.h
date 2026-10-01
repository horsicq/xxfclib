/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.lammps.org/dump.html */
#ifndef XX_LAMMPS_DUMP_H
#define XX_LAMMPS_DUMP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lammps_dump { Abstractformat format; } xx_lammps_dump;
XXFC_API void xx_lammps_dump_init(xx_lammps_dump *,xx_io_device *,int64_t);
XXFC_API xx_lammps_dump *xx_lammps_dump_create(xx_io_device *,int64_t);
XXFC_API void xx_lammps_dump_destroy(xx_lammps_dump *);
XXFC_API void xx_lammps_dump_free(xx_lammps_dump *);
XXFC_API bool xx_lammps_dump_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lammps_dump_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
