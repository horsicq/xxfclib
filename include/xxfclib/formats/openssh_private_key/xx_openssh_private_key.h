/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Bounded primary-layout reader. Payloads are never executed.
 */
#ifndef XX_OPENSSH_PRIVATE_KEY_H
#define XX_OPENSSH_PRIVATE_KEY_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_openssh_private_key { Abstractformat format; } xx_openssh_private_key;
XXFC_API void xx_openssh_private_key_init(xx_openssh_private_key *,xx_io_device *,int64_t);
XXFC_API xx_openssh_private_key *xx_openssh_private_key_create(xx_io_device *,int64_t);
XXFC_API void xx_openssh_private_key_destroy(xx_openssh_private_key *);
XXFC_API void xx_openssh_private_key_free(xx_openssh_private_key *);
XXFC_API bool xx_openssh_private_key_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_openssh_private_key_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_openssh_private_key_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_openssh_private_key_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_openssh_private_key_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_openssh_private_key_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_openssh_private_key_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
