/* SPDX-License-Identifier: MIT. Bounded floppy component reader. */
#ifndef XX_HXC_AFI_H
#define XX_HXC_AFI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hxc_afi {
    Abstractformat format;
} xx_hxc_afi;
XXFC_API void xx_hxc_afi_init(xx_hxc_afi *, xx_io_device *, int64_t);
XXFC_API xx_hxc_afi *xx_hxc_afi_create(xx_io_device *, int64_t);
XXFC_API void xx_hxc_afi_destroy(xx_hxc_afi *);
XXFC_API void xx_hxc_afi_free(xx_hxc_afi *);
XXFC_API bool xx_hxc_afi_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_hxc_afi_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
