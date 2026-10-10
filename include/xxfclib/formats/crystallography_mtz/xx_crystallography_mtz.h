/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/project-gemmi/gemmi/master/include/gemmi/mtz.hpp
 * CCP4 MTZ little-endian reflection tables: complete counted finite/missing-value columns and typed80-byte footer records, local datasets/columns/cell/symmetry metadata
 * and exact EOF. Original descriptor, reflection table and header records exported; batch blocks, big-endian and unknown extensions declined. Bounded32MiB input,4096
 * components and bounded work.
 */
#ifndef XX_CRYSTALLOGRAPHY_MTZ_H
#define XX_CRYSTALLOGRAPHY_MTZ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_crystallography_mtz {
    Abstractformat format;
} xx_crystallography_mtz;
XXFC_API void xx_crystallography_mtz_init(xx_crystallography_mtz *, xx_io_device *, int64_t);
XXFC_API xx_crystallography_mtz *xx_crystallography_mtz_create(xx_io_device *, int64_t);
XXFC_API void xx_crystallography_mtz_destroy(xx_crystallography_mtz *);
XXFC_API void xx_crystallography_mtz_free(xx_crystallography_mtz *);
XXFC_API bool xx_crystallography_mtz_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_crystallography_mtz_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_crystallography_mtz_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_crystallography_mtz_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_crystallography_mtz_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_crystallography_mtz_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_crystallography_mtz_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
