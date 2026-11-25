/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/panzi/HLLib/master/HLLib/SGAFile.h
 * Relic SGA6.0, one section/root folder, flat stored files with directory32-bit indices and26-byte file records, up to1024 members. Validates directory/table/name bounds
 * and stored CRC32. Exports file bytes with safe numeric names; zlib compression, nested/multiple roots, other versions and package trust unsupported.
 */
#ifndef XX_RELIC_SGA_H
#define XX_RELIC_SGA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_relic_sga {
    Abstractformat format;
} xx_relic_sga;
XXFC_API void xx_relic_sga_init(xx_relic_sga *, xx_io_device *, int64_t);
XXFC_API xx_relic_sga *xx_relic_sga_create(xx_io_device *, int64_t);
XXFC_API void xx_relic_sga_destroy(xx_relic_sga *);
XXFC_API void xx_relic_sga_free(xx_relic_sga *);
XXFC_API bool xx_relic_sga_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_relic_sga_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_relic_sga_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_relic_sga_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_relic_sga_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_relic_sga_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_relic_sga_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
