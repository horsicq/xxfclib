/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/NGSolve/netgen/master/libsrc/meshing/meshclass.cpp
 * Netgen VOL ASCII complete bounded dimension/point/surface/volume/edge mesh sections with finite coordinates and valid local indexes. Original mesh sections exported;
 * unsupported extensions/higher-order layouts declined. Bounded32MiB input storage and4096 exported components.
 */
#ifndef XX_NETGEN_VOL_H
#define XX_NETGEN_VOL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_netgen_vol {
    Abstractformat format;
} xx_netgen_vol;
XXFC_API void xx_netgen_vol_init(xx_netgen_vol *, xx_io_device *, int64_t);
XXFC_API xx_netgen_vol *xx_netgen_vol_create(xx_io_device *, int64_t);
XXFC_API void xx_netgen_vol_destroy(xx_netgen_vol *);
XXFC_API void xx_netgen_vol_free(xx_netgen_vol *);
XXFC_API bool xx_netgen_vol_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_netgen_vol_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_netgen_vol_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_netgen_vol_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_netgen_vol_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_netgen_vol_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_netgen_vol_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
