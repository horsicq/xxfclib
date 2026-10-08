/* SPDX-License-Identifier: MIT
 * Wire specification: https://vasp.at/wiki/POSCAR */
#ifndef XX_VASP_POSCAR_H
#define XX_VASP_POSCAR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_vasp_poscar { Abstractformat format; } xx_vasp_poscar;
XXFC_API void xx_vasp_poscar_init(xx_vasp_poscar *,xx_io_device *,int64_t);
XXFC_API xx_vasp_poscar *xx_vasp_poscar_create(xx_io_device *,int64_t);
XXFC_API void xx_vasp_poscar_destroy(xx_vasp_poscar *);
XXFC_API void xx_vasp_poscar_free(xx_vasp_poscar *);
XXFC_API bool xx_vasp_poscar_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_vasp_poscar_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_vasp_poscar_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_vasp_poscar_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_vasp_poscar_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
