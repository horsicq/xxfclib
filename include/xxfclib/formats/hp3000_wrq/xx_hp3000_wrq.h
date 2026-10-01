/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://www.microfocus.com/documentation/amc-archive/infoconnect-16-2/pdfdoc/infoconnect-help/infoconnect-help.pdf
 * Bounded independent carrier/container parser. No payload execution.
 */
#ifndef XX_HP3000_WRQ_H
#define XX_HP3000_WRQ_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hp3000_wrq { Abstractformat format; } xx_hp3000_wrq;
XXFC_API void xx_hp3000_wrq_init(xx_hp3000_wrq *,xx_io_device *,int64_t);
XXFC_API xx_hp3000_wrq *xx_hp3000_wrq_create(xx_io_device *,int64_t);
XXFC_API void xx_hp3000_wrq_destroy(xx_hp3000_wrq *);
XXFC_API void xx_hp3000_wrq_free(xx_hp3000_wrq *);
XXFC_API bool xx_hp3000_wrq_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_hp3000_wrq_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
