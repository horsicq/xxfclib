/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/DiscIO/WbfsBlob.cpp
 * One-disc WBFS volumes with 512-byte host sectors and 0.5-16MiB WBFS clusters, up to4096 allocated disc blocks. Checks declared physical length, first slot, logical
 * block map, unique physical clusters and Wii header copy. Exports stored disc-cluster components; sparse-disc reconstruction, split .wbf files, game filesystem and
 * decryption unsupported.
 */
#ifndef XX_NINTENDO_WBFS_H
#define XX_NINTENDO_WBFS_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_wbfs {
    Abstractformat format;
} xx_nintendo_wbfs;
XXFC_API void xx_nintendo_wbfs_init(xx_nintendo_wbfs *, xx_io_device *, int64_t);
XXFC_API xx_nintendo_wbfs *xx_nintendo_wbfs_create(xx_io_device *, int64_t);
XXFC_API void xx_nintendo_wbfs_destroy(xx_nintendo_wbfs *);
XXFC_API void xx_nintendo_wbfs_free(xx_nintendo_wbfs *);
XXFC_API bool xx_nintendo_wbfs_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nintendo_wbfs_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_wbfs_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_wbfs_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_wbfs_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_wbfs_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_wbfs_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
