/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_MSGPACK_H
#define XX_MSGPACK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_msgpack { Abstractformat format; } xx_msgpack;
XXFC_API void xx_msgpack_init(xx_msgpack *,xx_io_device *,int64_t);
XXFC_API xx_msgpack *xx_msgpack_create(xx_io_device *,int64_t);
XXFC_API void xx_msgpack_destroy(xx_msgpack *);
XXFC_API void xx_msgpack_free(xx_msgpack *);
XXFC_API bool xx_msgpack_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_msgpack_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
