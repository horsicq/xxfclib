/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_PARSEC_ARCHIVE_H
#define XX_PARSEC_ARCHIVE_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parsec's unnamed offset/size table containing contiguous RIB or SM8 files. */
typedef struct xx_parsec_archive {
    Abstractformat format;
} xx_parsec_archive;

XXFC_API void xx_parsec_archive_init(xx_parsec_archive *, xx_io_device *, int64_t);
XXFC_API xx_parsec_archive *xx_parsec_archive_create(xx_io_device *, int64_t);
XXFC_API void xx_parsec_archive_destroy(xx_parsec_archive *);
XXFC_API void xx_parsec_archive_free(xx_parsec_archive *);

#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_parsec_archive_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_parsec_archive_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_parsec_archive_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_parsec_archive_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_parsec_archive_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
