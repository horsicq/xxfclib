/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.lammps.org/dump.html */
#ifndef XX_LAMMPS_DUMP_H
#define XX_LAMMPS_DUMP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lammps_dump {
    Abstractformat format;
} xx_lammps_dump;
XXFC_API void xx_lammps_dump_init(xx_lammps_dump *, xx_io_device *, int64_t);
XXFC_API xx_lammps_dump *xx_lammps_dump_create(xx_io_device *, int64_t);
XXFC_API void xx_lammps_dump_destroy(xx_lammps_dump *);
XXFC_API void xx_lammps_dump_free(xx_lammps_dump *);
XXFC_API bool xx_lammps_dump_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_lammps_dump_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lammps_dump_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_lammps_dump_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lammps_dump_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lammps_dump_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_lammps_dump_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
