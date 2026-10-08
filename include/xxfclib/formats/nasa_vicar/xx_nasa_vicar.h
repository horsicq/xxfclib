/* SPDX-License-Identifier: MIT
 * Primary reference: https://www-mipl.jpl.nasa.gov/external/VICAR_file_fmt.pdf
 * NASA VICAR image subset: complete counted ASCII labels and BSQ/BIL/BIP sample layout with checked dimensions/record padding and original integer/finite floating planes/records. End-of-line labels, binary prefixes/header records and unknown label syntax declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_NASA_VICAR_H
#define XX_NASA_VICAR_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nasa_vicar {Abstractformat format;} xx_nasa_vicar;
XXFC_API void xx_nasa_vicar_init(xx_nasa_vicar *,xx_io_device *,int64_t);
XXFC_API xx_nasa_vicar *xx_nasa_vicar_create(xx_io_device *,int64_t);
XXFC_API void xx_nasa_vicar_destroy(xx_nasa_vicar *);
XXFC_API void xx_nasa_vicar_free(xx_nasa_vicar *);
XXFC_API bool xx_nasa_vicar_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nasa_vicar_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nasa_vicar_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nasa_vicar_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nasa_vicar_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
