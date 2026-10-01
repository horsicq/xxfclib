/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_MONGODB_WIRE_H
#define XX_MONGODB_WIRE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_mongodb_wire { Abstractformat format; } xx_mongodb_wire;
XXFC_API void xx_mongodb_wire_init(xx_mongodb_wire *,xx_io_device *,int64_t);
XXFC_API xx_mongodb_wire *xx_mongodb_wire_create(xx_io_device *,int64_t);
XXFC_API void xx_mongodb_wire_destroy(xx_mongodb_wire *);
XXFC_API void xx_mongodb_wire_free(xx_mongodb_wire *);
XXFC_API bool xx_mongodb_wire_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_mongodb_wire_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
