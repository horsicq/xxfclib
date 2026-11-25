/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/zeldamods/byml-v2/master/byml/byml.py
 * BYAML version2, either endian, flat/nested maps and arrays of string/bool/int32/uint32/float/null values, up to1024 nodes/depth32 and1024 strings per table. Exports
 * bounded encoded tables/nodes; validates key/string indices and sorted keys. Shared nodes, binary/64-bit values, newer revisions and application interpretation
 * unsupported.
 */
#ifndef XX_NINTENDO_BYAML_H
#define XX_NINTENDO_BYAML_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_byaml {
    Abstractformat format;
} xx_nintendo_byaml;
XXFC_API void xx_nintendo_byaml_init(xx_nintendo_byaml *, xx_io_device *, int64_t);
XXFC_API xx_nintendo_byaml *xx_nintendo_byaml_create(xx_io_device *, int64_t);
XXFC_API void xx_nintendo_byaml_destroy(xx_nintendo_byaml *);
XXFC_API void xx_nintendo_byaml_free(xx_nintendo_byaml *);
XXFC_API bool xx_nintendo_byaml_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_nintendo_byaml_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_byaml_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_byaml_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_byaml_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_byaml_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_byaml_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
