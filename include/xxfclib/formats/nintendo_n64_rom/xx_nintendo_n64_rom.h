/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://n64dev.org/n64crc.html
 * Native big-endian N64 images with standard80371240 header, cached entry address and CIC6101/6102 data checksums over the first1MiB at0x1000. File1MiB+4KiB-64MiB in512-byte units. Exports original header, IPL3 boot block and ROM body; byte-swapped images, other CIC checksums, boot-code authentication, emulation and execution unsupported.
 */
#ifndef XX_NINTENDO_N64_ROM_H
#define XX_NINTENDO_N64_ROM_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_n64_rom { Abstractformat format; } xx_nintendo_n64_rom;
XXFC_API void xx_nintendo_n64_rom_init(xx_nintendo_n64_rom *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_n64_rom *xx_nintendo_n64_rom_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_n64_rom_destroy(xx_nintendo_n64_rom *);
XXFC_API void xx_nintendo_n64_rom_free(xx_nintendo_n64_rom *);
XXFC_API bool xx_nintendo_n64_rom_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_n64_rom_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_n64_rom_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_n64_rom_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_n64_rom_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
