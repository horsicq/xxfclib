/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * install4j Windows installers (ej-technologies): the launcher carries its
 * configuration, then a table of named data entries, each an .lzma stream
 * around a ZIP of the installed files.
 */
#ifndef XX_INSTALL4J_H
#define XX_INSTALL4J_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_install4j {
    Abstractformat format;
    void *nested; /* The decoded entries' ZIP readers. */
} xx_install4j;
XXFC_API void xx_install4j_init(xx_install4j *, xx_io_device *, int64_t);
XXFC_API xx_install4j *xx_install4j_create(xx_io_device *, int64_t);
XXFC_API void xx_install4j_destroy(xx_install4j *);
XXFC_API void xx_install4j_free(xx_install4j *);
XXFC_API bool xx_install4j_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_install4j_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_install4j_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_install4j_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_install4j_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_install4j_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_install4j_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
