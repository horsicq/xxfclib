/* Copyright (c)2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT. Bounded typed components; payloads never executed.
 */
#ifndef XX_RTP_RTCP_H
#define XX_RTP_RTCP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_rtp_rtcp {Abstractformat format;} xx_rtp_rtcp;
XXFC_API void xx_rtp_rtcp_init(xx_rtp_rtcp *,xx_io_device *,int64_t);
XXFC_API xx_rtp_rtcp *xx_rtp_rtcp_create(xx_io_device *,int64_t);
XXFC_API void xx_rtp_rtcp_destroy(xx_rtp_rtcp *);
XXFC_API void xx_rtp_rtcp_free(xx_rtp_rtcp *);
XXFC_API bool xx_rtp_rtcp_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_rtp_rtcp_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
