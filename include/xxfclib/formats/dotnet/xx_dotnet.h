/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XXFCLIB_FORMAT_DOTNET_H
#define XXFCLIB_FORMAT_DOTNET_H

#include "xxfclib/formats/pe/xx_pe.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum xx_dotnet_data_struct_id_e {
    XX_DOTNET_DATA_STRUCT_UNKNOWN = 0,
    XX_DOTNET_DATA_STRUCT_COR20_HEADER = 112,
    XX_DOTNET_DATA_STRUCT_CLR_METADATA_ROOT = 113,
    XX_DOTNET_DATA_STRUCT_CLR_METADATA_STORAGE_HEADER = 114,
    XX_DOTNET_DATA_STRUCT_CLR_STREAM_HEADER = 115,
    XX_DOTNET_DATA_STRUCT_CLR_VTABLE_FIXUP = 116,
    XX_DOTNET_DATA_STRUCT_CLR_RAW = 117,
    XX_DOTNET_DATA_STRUCT_LAST = XX_DOTNET_DATA_STRUCT_CLR_RAW
} xx_dotnet_data_struct_id_t;

/* PE is the first member so inherited PE callbacks can use the same pointer. */
typedef struct xx_dotnet {
    xx_pe pe;
} xx_dotnet;
typedef xx_dotnet xx_dotnet_t;
typedef xx_dotnet XDotNet;

XXFC_API void xx_dotnet_init(xx_dotnet *, xx_io_device *, int64_t base_address);
XXFC_API xx_dotnet *xx_dotnet_create(xx_io_device *, int64_t base_address);
XXFC_API void xx_dotnet_destroy(xx_dotnet *);
XXFC_API void xx_dotnet_free(xx_dotnet *);
XXFC_API bool xx_dotnet_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_dotnet_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API int64_t xx_dotnet_get_format_size(Abstractformat *, xx_pd_struct *);

static inline xx_pe *xx_dotnet_to_pe(xx_dotnet *reader) { return reader ? &reader->pe : NULL; }
static inline const xx_pe *xx_dotnet_to_pe_const(const xx_dotnet *reader) { return reader ? &reader->pe : NULL; }
static inline Abstractformat *xx_dotnet_to_format(xx_dotnet *reader) { return reader ? &reader->pe.format : NULL; }
static inline const Abstractformat *xx_dotnet_to_format_const(const xx_dotnet *reader) { return reader ? &reader->pe.format : NULL; }

#include "xxfclib/formats/dotnet/xx_dotnet_inspect.h"
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_dotnet_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_dotnet_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_dotnet_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_dotnet_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_dotnet_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
