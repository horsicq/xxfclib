/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_DCERPC_PDU_H
#define XX_DCERPC_PDU_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_dcerpc_pdu {Abstractformat format;} xx_dcerpc_pdu;
XXFC_API void xx_dcerpc_pdu_init(xx_dcerpc_pdu *,xx_io_device *,int64_t);
XXFC_API xx_dcerpc_pdu *xx_dcerpc_pdu_create(xx_io_device *,int64_t);
XXFC_API void xx_dcerpc_pdu_destroy(xx_dcerpc_pdu *);
XXFC_API void xx_dcerpc_pdu_free(xx_dcerpc_pdu *);
XXFC_API bool xx_dcerpc_pdu_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_dcerpc_pdu_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
