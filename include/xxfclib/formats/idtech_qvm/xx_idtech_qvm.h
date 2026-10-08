/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/id-Software/Quake-III-Arena/master/code/qcommon/qfiles.h
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_IDTECH_QVM_H
#define XX_IDTECH_QVM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_idtech_qvm { Abstractformat format; } xx_idtech_qvm;
XXFC_API void xx_idtech_qvm_init(xx_idtech_qvm *,xx_io_device *,int64_t);
XXFC_API xx_idtech_qvm *xx_idtech_qvm_create(xx_io_device *,int64_t);
XXFC_API void xx_idtech_qvm_destroy(xx_idtech_qvm *);
XXFC_API void xx_idtech_qvm_free(xx_idtech_qvm *);
XXFC_API bool xx_idtech_qvm_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_idtech_qvm_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_idtech_qvm_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_idtech_qvm_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_idtech_qvm_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
