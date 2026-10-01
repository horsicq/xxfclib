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
#endif
