/* SPDX-License-Identifier: MIT
 * Primary reference: https://raw.githubusercontent.com/ImageMagick/ImageMagick/main/coders/uil.c
 * Motif UIL static icon tables: complete typed color-table/icon definitions, unique identifiers/symbols and resolved color-table references, equal bounded row widths and defined pixel symbols. Original palette/icon records and decoded RGBA bitmap exported; arbitrary UIL modules/procedures/includes declined.
 * Bounded32MiB input,4096 components and bounded work.
 */
#ifndef XX_MOTIF_UIL_H
#define XX_MOTIF_UIL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_motif_uil {Abstractformat format;} xx_motif_uil;
XXFC_API void xx_motif_uil_init(xx_motif_uil *,xx_io_device *,int64_t);
XXFC_API xx_motif_uil *xx_motif_uil_create(xx_io_device *,int64_t);
XXFC_API void xx_motif_uil_destroy(xx_motif_uil *);
XXFC_API void xx_motif_uil_free(xx_motif_uil *);
XXFC_API bool xx_motif_uil_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_motif_uil_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_motif_uil_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_motif_uil_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_motif_uil_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_motif_uil_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_motif_uil_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
