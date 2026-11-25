/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/AcademySoftwareFoundation/OpenColorIO/main/src/OpenColorIO/fileformats/FileFormatIridasCube.cpp
 * IRIDAS CUBE LUT: complete finite1D/3D color tables, exact declared sample count, ordered finite domains and bounded quoted title. Original header/table exported; LUT
 * application unsupported. Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_IRIDAS_CUBE_LUT_H
#define XX_IRIDAS_CUBE_LUT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_iridas_cube_lut {
    Abstractformat format;
} xx_iridas_cube_lut;
XXFC_API void xx_iridas_cube_lut_init(xx_iridas_cube_lut *, xx_io_device *, int64_t);
XXFC_API xx_iridas_cube_lut *xx_iridas_cube_lut_create(xx_io_device *, int64_t);
XXFC_API void xx_iridas_cube_lut_destroy(xx_iridas_cube_lut *);
XXFC_API void xx_iridas_cube_lut_free(xx_iridas_cube_lut *);
XXFC_API bool xx_iridas_cube_lut_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_iridas_cube_lut_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_iridas_cube_lut_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_iridas_cube_lut_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_iridas_cube_lut_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_iridas_cube_lut_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_iridas_cube_lut_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
