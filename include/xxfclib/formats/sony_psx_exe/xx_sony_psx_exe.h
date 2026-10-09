/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://psx-spx.consoledev.net/cdromfileformats/#filenameexe-general-purpose-executable
 * PS-X EXE with cached 2MiB RAM load addresses and sector-aligned code/data. Exports the 2048-byte header and stored load image; no relocation, loading, self-unpacking, BIOS calls or execution.
 */
#ifndef XX_SONY_PSX_EXE_H
#define XX_SONY_PSX_EXE_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sony_psx_exe { Abstractformat format; } xx_sony_psx_exe;
XXFC_API void xx_sony_psx_exe_init(xx_sony_psx_exe *,xx_io_device *,int64_t);
XXFC_API xx_sony_psx_exe *xx_sony_psx_exe_create(xx_io_device *,int64_t);
XXFC_API void xx_sony_psx_exe_destroy(xx_sony_psx_exe *);
XXFC_API void xx_sony_psx_exe_free(xx_sony_psx_exe *);
XXFC_API bool xx_sony_psx_exe_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sony_psx_exe_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sony_psx_exe_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_sony_psx_exe_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sony_psx_exe_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sony_psx_exe_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_sony_psx_exe_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
