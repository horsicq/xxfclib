/* SPDX-License-Identifier: MIT
 * Wire specification: https://github.com/AcademySoftwareFoundation/OpenColorIO/blob/main/src/OpenColorIO/fileformats/FileFormatSpi1D.cpp */
#ifndef XX_LUT_SPI1D_H
#define XX_LUT_SPI1D_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lut_spi1d { Abstractformat format; } xx_lut_spi1d;
XXFC_API void xx_lut_spi1d_init(xx_lut_spi1d *,xx_io_device *,int64_t);
XXFC_API xx_lut_spi1d *xx_lut_spi1d_create(xx_io_device *,int64_t);
XXFC_API void xx_lut_spi1d_destroy(xx_lut_spi1d *);
XXFC_API void xx_lut_spi1d_free(xx_lut_spi1d *);
XXFC_API bool xx_lut_spi1d_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lut_spi1d_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
