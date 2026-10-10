/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.lammps.org/read_data.html */
#ifndef XX_LAMMPS_DATA_H
#define XX_LAMMPS_DATA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lammps_data {
    Abstractformat format;
} xx_lammps_data;
XXFC_API void xx_lammps_data_init(xx_lammps_data *, xx_io_device *, int64_t);
XXFC_API xx_lammps_data *xx_lammps_data_create(xx_io_device *, int64_t);
XXFC_API void xx_lammps_data_destroy(xx_lammps_data *);
XXFC_API void xx_lammps_data_free(xx_lammps_data *);
XXFC_API bool xx_lammps_data_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_lammps_data_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lammps_data_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_lammps_data_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lammps_data_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lammps_data_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_lammps_data_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
