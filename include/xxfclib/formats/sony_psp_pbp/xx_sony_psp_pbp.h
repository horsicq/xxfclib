/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/hrydgard/ppsspp/master/Core/ELF/PBPReader.cpp
 * PSP PBP version1.0 homebrew packages with eight monotonically ordered section offsets. Requires bounded PARAM.SFO key/type/value tables and complete32-bit
 * little-endian MIPS ELF DATA.PSP, no DATA.PSAR. Checks ELF program/section extents and entry point. Exports original section bytes; encrypted PSP/PSAR, ISO
 * interpretation and execution unsupported.
 */
#ifndef XX_SONY_PSP_PBP_H
#define XX_SONY_PSP_PBP_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sony_psp_pbp {
    Abstractformat format;
} xx_sony_psp_pbp;
XXFC_API void xx_sony_psp_pbp_init(xx_sony_psp_pbp *, xx_io_device *, int64_t);
XXFC_API xx_sony_psp_pbp *xx_sony_psp_pbp_create(xx_io_device *, int64_t);
XXFC_API void xx_sony_psp_pbp_destroy(xx_sony_psp_pbp *);
XXFC_API void xx_sony_psp_pbp_free(xx_sony_psp_pbp *);
XXFC_API bool xx_sony_psp_pbp_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sony_psp_pbp_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sony_psp_pbp_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sony_psp_pbp_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sony_psp_pbp_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sony_psp_pbp_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sony_psp_pbp_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
