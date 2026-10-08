/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/FNA-XNA/FAudio/master/src/FACT_internal.c
 * Little-endian Windows XACT content versions43-46/tool43, single-sound simple cues and12-byte simple sound entries, up to1024 each and32 wave-bank names. Validates cue-to-sound and sound-to-bank references. Exports encoded cue/sound/name records; complex cues/tracks/RPC/DSP, cue-name/hash tables and audio execution unsupported.
 */
#ifndef XX_MICROSOFT_XSB_H
#define XX_MICROSOFT_XSB_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_microsoft_xsb { Abstractformat format; } xx_microsoft_xsb;
XXFC_API void xx_microsoft_xsb_init(xx_microsoft_xsb *,xx_io_device *,int64_t);
XXFC_API xx_microsoft_xsb *xx_microsoft_xsb_create(xx_io_device *,int64_t);
XXFC_API void xx_microsoft_xsb_destroy(xx_microsoft_xsb *);
XXFC_API void xx_microsoft_xsb_free(xx_microsoft_xsb *);
XXFC_API bool xx_microsoft_xsb_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_microsoft_xsb_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_microsoft_xsb_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_microsoft_xsb_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_microsoft_xsb_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
