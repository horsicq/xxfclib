/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/ParmEd/ParmEd/blob/master/parmed/amber/_amberparm.py */
#ifndef XX_AMBER_PRMTOP_H
#define XX_AMBER_PRMTOP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_amber_prmtop { Abstractformat format; } xx_amber_prmtop;
XXFC_API void xx_amber_prmtop_init(xx_amber_prmtop *,xx_io_device *,int64_t);
XXFC_API xx_amber_prmtop *xx_amber_prmtop_create(xx_io_device *,int64_t);
XXFC_API void xx_amber_prmtop_destroy(xx_amber_prmtop *);
XXFC_API void xx_amber_prmtop_free(xx_amber_prmtop *);
XXFC_API bool xx_amber_prmtop_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_amber_prmtop_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_amber_prmtop_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_amber_prmtop_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_amber_prmtop_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_amber_prmtop_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_amber_prmtop_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
