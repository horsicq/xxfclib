/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://github.com/jopadan/termpod/blob/master/include/termpod/pod.hpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_TERMINALREALITY_POD_H
#define XX_TERMINALREALITY_POD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_terminalreality_pod { Abstractformat format; } xx_terminalreality_pod;
XXFC_API void xx_terminalreality_pod_init(xx_terminalreality_pod *,xx_io_device *,int64_t);
XXFC_API xx_terminalreality_pod *xx_terminalreality_pod_create(xx_io_device *,int64_t);
XXFC_API void xx_terminalreality_pod_destroy(xx_terminalreality_pod *);
XXFC_API void xx_terminalreality_pod_free(xx_terminalreality_pod *);
XXFC_API bool xx_terminalreality_pod_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_terminalreality_pod_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_terminalreality_pod_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_terminalreality_pod_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_terminalreality_pod_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
