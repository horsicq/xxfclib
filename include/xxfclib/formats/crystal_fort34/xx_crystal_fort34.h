/* SPDX-License-Identifier: MIT
 * Wire specification: https://docs.ase-lib.org/_modules/ase/io/crystal.html */
#ifndef XX_CRYSTAL_FORT34_H
#define XX_CRYSTAL_FORT34_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_crystal_fort34 { Abstractformat format; } xx_crystal_fort34;
XXFC_API void xx_crystal_fort34_init(xx_crystal_fort34 *,xx_io_device *,int64_t);
XXFC_API xx_crystal_fort34 *xx_crystal_fort34_create(xx_io_device *,int64_t);
XXFC_API void xx_crystal_fort34_destroy(xx_crystal_fort34 *);
XXFC_API void xx_crystal_fort34_free(xx_crystal_fort34 *);
XXFC_API bool xx_crystal_fort34_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_crystal_fort34_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_crystal_fort34_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_crystal_fort34_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_crystal_fort34_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
