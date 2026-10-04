/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_HXC_QD_H
#define XX_HXC_QD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hxc_qd {Abstractformat format;} xx_hxc_qd;
XXFC_API void xx_hxc_qd_init(xx_hxc_qd *,xx_io_device *,int64_t);
XXFC_API xx_hxc_qd *xx_hxc_qd_create(xx_io_device *,int64_t);
XXFC_API void xx_hxc_qd_destroy(xx_hxc_qd *);
XXFC_API void xx_hxc_qd_free(xx_hxc_qd *);
XXFC_API bool xx_hxc_qd_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_hxc_qd_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
