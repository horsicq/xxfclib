/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/Boot/DolReader.h
 * DOL headers with first text section at0x100, up to7 text/11 data sections in cached MEM1 or MEM2 addresses. Exports separate stored executable sections; validates
 * file/memory ranges and entry point. Alternate first offsets, Ancast, relocation, loading and execution unsupported.
 */
#ifndef XX_NINTENDO_DOL_H
#define XX_NINTENDO_DOL_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_dol {
    Abstractformat format;
} xx_nintendo_dol;
XXFC_API void xx_nintendo_dol_init(xx_nintendo_dol *, xx_io_device *, int64_t);
XXFC_API xx_nintendo_dol *xx_nintendo_dol_create(xx_io_device *, int64_t);
XXFC_API void xx_nintendo_dol_destroy(xx_nintendo_dol *);
XXFC_API void xx_nintendo_dol_free(xx_nintendo_dol *);
XXFC_API bool xx_nintendo_dol_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nintendo_dol_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_dol_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_dol_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_dol_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_dol_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_dol_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
