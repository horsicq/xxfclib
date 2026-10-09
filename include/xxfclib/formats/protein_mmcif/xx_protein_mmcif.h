/* SPDX-License-Identifier: MIT
 * Wire specification: https://mmcif.wwpdb.org/docs/tutorials/mechanics/pdbx-mmcif-syntax.html */
#ifndef XX_PROTEIN_MMCIF_H
#define XX_PROTEIN_MMCIF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_protein_mmcif { Abstractformat format; } xx_protein_mmcif;
XXFC_API void xx_protein_mmcif_init(xx_protein_mmcif *,xx_io_device *,int64_t);
XXFC_API xx_protein_mmcif *xx_protein_mmcif_create(xx_io_device *,int64_t);
XXFC_API void xx_protein_mmcif_destroy(xx_protein_mmcif *);
XXFC_API void xx_protein_mmcif_free(xx_protein_mmcif *);
XXFC_API bool xx_protein_mmcif_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_protein_mmcif_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_protein_mmcif_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_protein_mmcif_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_protein_mmcif_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_protein_mmcif_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_protein_mmcif_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
