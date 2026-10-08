/* SPDX-License-Identifier: MIT
 * Wire specification: https://archive.gfjc.fiu.edu/workshops/resources/literature/ABIF_File_Format.pdf */
#ifndef XX_SEQUENCING_ABIF_H
#define XX_SEQUENCING_ABIF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sequencing_abif { Abstractformat format; } xx_sequencing_abif;
XXFC_API void xx_sequencing_abif_init(xx_sequencing_abif *,xx_io_device *,int64_t);
XXFC_API xx_sequencing_abif *xx_sequencing_abif_create(xx_io_device *,int64_t);
XXFC_API void xx_sequencing_abif_destroy(xx_sequencing_abif *);
XXFC_API void xx_sequencing_abif_free(xx_sequencing_abif *);
XXFC_API bool xx_sequencing_abif_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sequencing_abif_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sequencing_abif_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sequencing_abif_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sequencing_abif_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
