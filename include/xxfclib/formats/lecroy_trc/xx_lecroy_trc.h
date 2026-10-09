/* SPDX-License-Identifier: MIT
 * Wire specification: https://www.teledynelecroy.com/support/knowledgebase.aspx?docid=556 */
#ifndef XX_LECROY_TRC_H
#define XX_LECROY_TRC_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_lecroy_trc { Abstractformat format; } xx_lecroy_trc;
XXFC_API void xx_lecroy_trc_init(xx_lecroy_trc *,xx_io_device *,int64_t);
XXFC_API xx_lecroy_trc *xx_lecroy_trc_create(xx_io_device *,int64_t);
XXFC_API void xx_lecroy_trc_destroy(xx_lecroy_trc *);
XXFC_API void xx_lecroy_trc_free(xx_lecroy_trc *);
XXFC_API bool xx_lecroy_trc_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_lecroy_trc_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_lecroy_trc_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_lecroy_trc_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_lecroy_trc_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_lecroy_trc_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_lecroy_trc_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
