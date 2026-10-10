/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_LEGACY_ARCHIVE_INFO_H
#define XX_LEGACY_ARCHIVE_INFO_H
#include "xxfclib/formats/xx_format.h"
/* Shared public state for bounded legacy archive readers. All offsets are
 * interpreted relative to format.base_address. Input devices remain borrowed. */
typedef struct xx_legacy_archive_info {
    Abstractformat format;
    const xx_list_s *parse_options; /* borrowed only for an active operation */
    uint64_t number_of_records;
    bool incomplete;
    const char *note;
} xx_legacy_archive_info;
#ifdef __cplusplus
#define XX_LEGACY_ARCHIVE_API extern "C" XXFC_API
#else
#define XX_LEGACY_ARCHIVE_API XXFC_API
#endif
#define XX_LEGACY_ARCHIVE_DECLARE(stem)                                                \
    typedef xx_legacy_archive_info xx_##stem;                                          \
    XX_LEGACY_ARCHIVE_API void xx_##stem##_init(xx_##stem *, xx_io_device *, int64_t); \
    XX_LEGACY_ARCHIVE_API xx_##stem *xx_##stem##_create(xx_io_device *, int64_t);      \
    XX_LEGACY_ARCHIVE_API void xx_##stem##_destroy(xx_##stem *);                       \
    XX_LEGACY_ARCHIVE_API void xx_##stem##_free(xx_##stem *)
#endif
