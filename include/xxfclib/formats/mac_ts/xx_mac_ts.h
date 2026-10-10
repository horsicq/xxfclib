/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Native bounded read-only Macintosh TS partition map reader. No payload is executed.
 */
#ifndef XX_MAC_TS_H
#define XX_MAC_TS_H
#include "xxfclib/formats/apple_family/xx_apple_family.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_apple_family_info xx_mac_ts;
typedef xx_mac_ts xx_mac_ts_t;
XXFC_API void xx_mac_ts_init(xx_mac_ts *, xx_io_device *, int64_t);
XXFC_API xx_mac_ts *xx_mac_ts_create(xx_io_device *, int64_t);
XXFC_API void xx_mac_ts_destroy(xx_mac_ts *);
XXFC_API void xx_mac_ts_free(xx_mac_ts *);
XXFC_API bool xx_mac_ts_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_mac_ts_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_mac_ts_extract_record_to_device(Abstractformat *, xx_archive_record_state *, xx_io_device *, xx_pd_struct *);
static inline Abstractformat *xx_mac_ts_to_format(xx_mac_ts *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
