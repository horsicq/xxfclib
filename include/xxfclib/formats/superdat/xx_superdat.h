/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_SUPERDAT_H
#define XX_SUPERDAT_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Network Associates/McAfee/Trellix SuperDAT LH1 packages. Reads member names,
 * sizes, dates and manifest CRC32 values; streams decoding with bounded RAM.
 * Supports original/v1.2 signing/footer variants and modern resource-plus-payload
 * layouts. Vendor DAT checksums with unknown domain appear as info comments.
 * Never executes the installer. Other archive generations fail explicitly. */
typedef struct xx_superdat {
    Abstractformat format;
    void *index;
    uint64_t generation;
} xx_superdat;
XXFC_API void xx_superdat_init(xx_superdat *, xx_io_device *, int64_t);
XXFC_API xx_superdat *xx_superdat_create(xx_io_device *, int64_t);
XXFC_API void xx_superdat_destroy(xx_superdat *);
XXFC_API void xx_superdat_free(xx_superdat *);
XXFC_API bool xx_superdat_has_candidate_device(xx_io_device *, int64_t);
#ifdef __cplusplus
}
#endif
#endif
