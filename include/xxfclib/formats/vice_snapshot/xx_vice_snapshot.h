/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Independent bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_VICE_SNAPSHOT_H
#define XX_VICE_SNAPSHOT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_vice_snapshot { Abstractformat format; } xx_vice_snapshot;
XXFC_API void xx_vice_snapshot_init(xx_vice_snapshot *,xx_io_device *,int64_t);
XXFC_API xx_vice_snapshot *xx_vice_snapshot_create(xx_io_device *,int64_t);
XXFC_API void xx_vice_snapshot_destroy(xx_vice_snapshot *);
XXFC_API void xx_vice_snapshot_free(xx_vice_snapshot *);
XXFC_API bool xx_vice_snapshot_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_vice_snapshot_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_vice_snapshot_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_vice_snapshot_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_vice_snapshot_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_vice_snapshot_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_vice_snapshot_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
