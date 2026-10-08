/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/main/src/OpenColorIO/fileformats/FileFormatSpi3D.cpp */
#ifndef XX_LUT_SPI3D_H
#define XX_LUT_SPI3D_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lut_spi3d { Abstractformat format; } xx_lut_spi3d;
XXFC_API void xx_lut_spi3d_init(xx_lut_spi3d *,xx_io_device *,int64_t);
XXFC_API xx_lut_spi3d *xx_lut_spi3d_create(xx_io_device *,int64_t);
XXFC_API void xx_lut_spi3d_destroy(xx_lut_spi3d *);
XXFC_API void xx_lut_spi3d_free(xx_lut_spi3d *);
XXFC_API bool xx_lut_spi3d_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lut_spi3d_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lut_spi3d_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lut_spi3d_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lut_spi3d_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
