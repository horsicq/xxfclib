/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary grammar. Payloads are never executed.
 */
#ifndef XX_GDBM_DUMP_H
#define XX_GDBM_DUMP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_gdbm_dump {Abstractformat format;} xx_gdbm_dump;
XXFC_API void xx_gdbm_dump_init(xx_gdbm_dump *,xx_io_device *,int64_t);
XXFC_API xx_gdbm_dump *xx_gdbm_dump_create(xx_io_device *,int64_t);
XXFC_API void xx_gdbm_dump_destroy(xx_gdbm_dump *);
XXFC_API void xx_gdbm_dump_free(xx_gdbm_dump *);
XXFC_API bool xx_gdbm_dump_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_gdbm_dump_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_gdbm_dump_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_gdbm_dump_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_gdbm_dump_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
