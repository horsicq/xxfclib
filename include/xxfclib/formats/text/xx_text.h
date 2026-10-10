/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_TEXT_H
#define XX_TEXT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum xx_text_encoding {
    XX_TEXT_ENCODING_UNKNOWN = 0,
    XX_TEXT_ENCODING_ASCII,
    XX_TEXT_ENCODING_UTF8,
    XX_TEXT_ENCODING_UTF16_LE,
    XX_TEXT_ENCODING_UTF16_BE,
    XX_TEXT_ENCODING_UTF32_LE,
    XX_TEXT_ENCODING_UTF32_BE
} xx_text_encoding;
typedef struct xx_text_info {
    xx_text_encoding encoding;
    uint8_t bom_size;
    bool has_bom;
    uint64_t character_count; /* Unicode scalar count, excluding an initial BOM. */
} xx_text_info;
typedef struct xx_text { Abstractformat format; xx_text_info info; } xx_text;
XXFC_API void xx_text_init(xx_text *, xx_io_device *, int64_t);
XXFC_API xx_text *xx_text_create(xx_io_device *, int64_t);
XXFC_API void xx_text_destroy(xx_text *);
XXFC_API void xx_text_free(xx_text *);
XXFC_API bool xx_text_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_text_handle_base_info(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_text_get_info(xx_text *, xx_text_info *, xx_pd_struct *);
XXFC_API const char *xx_text_encoding_name(xx_text_encoding);
/* For explicitly selected TEXT readers only: not registered as a global
 * autodetector, which could hide JSON/XML/source and other specific formats. */
XXFC_API xx_file_type_t xx_text_detect(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
