/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/uclouvain/openjpeg/master/src/bin/jp2/convert.c
 * PGX gray planes: exact ML/LM signed or unsigned1..16bit descriptor, bounded dimensions and precision-consistent encoded sample range. Original encoded plane and descriptor exported; no color rendering.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_JPEG2000_PGX_H
#define XX_JPEG2000_PGX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_jpeg2000_pgx {Abstractformat format;} xx_jpeg2000_pgx;
XXFC_API void xx_jpeg2000_pgx_init(xx_jpeg2000_pgx *,xx_io_device *,int64_t);
XXFC_API xx_jpeg2000_pgx *xx_jpeg2000_pgx_create(xx_io_device *,int64_t);
XXFC_API void xx_jpeg2000_pgx_destroy(xx_jpeg2000_pgx *);
XXFC_API void xx_jpeg2000_pgx_free(xx_jpeg2000_pgx *);
XXFC_API bool xx_jpeg2000_pgx_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_jpeg2000_pgx_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_jpeg2000_pgx_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_jpeg2000_pgx_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_jpeg2000_pgx_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_jpeg2000_pgx_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_jpeg2000_pgx_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
