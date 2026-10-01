/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/AcademySoftwareFoundation/OpenColorIO/main/src/OpenColorIO/fileformats/FileFormatIridasCube.cpp
 * IRIDAS CUBE LUT: complete finite1D/3D color tables, exact declared sample count, ordered finite domains and bounded quoted title. Original header/table exported; LUT application unsupported.
 * Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_IRIDAS_CUBE_LUT_H
#define XX_IRIDAS_CUBE_LUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_iridas_cube_lut {Abstractformat format;} xx_iridas_cube_lut;
XXFC_API void xx_iridas_cube_lut_init(xx_iridas_cube_lut *,xx_io_device *,int64_t);
XXFC_API xx_iridas_cube_lut *xx_iridas_cube_lut_create(xx_io_device *,int64_t);
XXFC_API void xx_iridas_cube_lut_destroy(xx_iridas_cube_lut *);
XXFC_API void xx_iridas_cube_lut_free(xx_iridas_cube_lut *);
XXFC_API bool xx_iridas_cube_lut_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_iridas_cube_lut_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
