/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded component reader; input bytes are never executed or played.
 */
#ifndef XX_NINTENDO_FDS_H
#define XX_NINTENDO_FDS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_fds { Abstractformat format; } xx_nintendo_fds;
XXFC_API void xx_nintendo_fds_init(xx_nintendo_fds *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_fds *xx_nintendo_fds_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_fds_destroy(xx_nintendo_fds *);
XXFC_API void xx_nintendo_fds_free(xx_nintendo_fds *);
XXFC_API bool xx_nintendo_fds_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_fds_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_fds_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_fds_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_fds_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_fds_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_fds_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
