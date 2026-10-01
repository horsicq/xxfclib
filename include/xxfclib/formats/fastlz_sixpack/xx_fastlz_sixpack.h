/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_FASTLZ_SIXPACK_H
#define XX_FASTLZ_SIXPACK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_fastlz_sixpack { Abstractformat format; } xx_fastlz_sixpack;
XXFC_API void xx_fastlz_sixpack_init(xx_fastlz_sixpack *,xx_io_device *,int64_t);
XXFC_API xx_fastlz_sixpack *xx_fastlz_sixpack_create(xx_io_device *,int64_t);
XXFC_API void xx_fastlz_sixpack_destroy(xx_fastlz_sixpack *);
XXFC_API void xx_fastlz_sixpack_free(xx_fastlz_sixpack *);
XXFC_API bool xx_fastlz_sixpack_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_fastlz_sixpack_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
