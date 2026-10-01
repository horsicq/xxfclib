/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.lammps.org/read_data.html */
#ifndef XX_LAMMPS_DATA_H
#define XX_LAMMPS_DATA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lammps_data { Abstractformat format; } xx_lammps_data;
XXFC_API void xx_lammps_data_init(xx_lammps_data *,xx_io_device *,int64_t);
XXFC_API xx_lammps_data *xx_lammps_data_create(xx_io_device *,int64_t);
XXFC_API void xx_lammps_data_destroy(xx_lammps_data *);
XXFC_API void xx_lammps_data_free(xx_lammps_data *);
XXFC_API bool xx_lammps_data_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lammps_data_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
