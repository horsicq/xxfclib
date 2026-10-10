/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_DCERPC_PDU_H
#define XX_DCERPC_PDU_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dcerpc_pdu {
    Abstractformat format;
} xx_dcerpc_pdu;
XXFC_API void xx_dcerpc_pdu_init(xx_dcerpc_pdu *, xx_io_device *, int64_t);
XXFC_API xx_dcerpc_pdu *xx_dcerpc_pdu_create(xx_io_device *, int64_t);
XXFC_API void xx_dcerpc_pdu_destroy(xx_dcerpc_pdu *);
XXFC_API void xx_dcerpc_pdu_free(xx_dcerpc_pdu *);
XXFC_API bool xx_dcerpc_pdu_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dcerpc_pdu_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dcerpc_pdu_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dcerpc_pdu_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dcerpc_pdu_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dcerpc_pdu_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dcerpc_pdu_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
