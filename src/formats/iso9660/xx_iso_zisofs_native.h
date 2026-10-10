/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_ISO_ZISOFS_NATIVE_H
#define XX_ISO_ZISOFS_NATIVE_H

#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct xx_iso_zisofs_info_s {
    uint64_t uncompressed_size;
    uint8_t version;      /* 1: zisofs, 2: zisofs2 */
    uint8_t algorithm;    /* 1: independent zlib blocks */
    uint8_t header_words; /* header size divided by four */
    uint8_t block_shift;  /* log2 of uncompressed block size */
} xx_iso_zisofs_info;

/* Inspect one ISO 9660 directory record's SUSP entries. A valid ZF/Z2 marker
 * sets present. A malformed compression marker fails instead of exposing its
 * encoded bytes as an ordinary file. */
bool xx_iso_zisofs_parse_record(const uint8_t *record, size_t record_size, xx_iso_zisofs_info *info, bool *present);

/* Stream a single-extent zisofs file into an already-open destination. The
 * source cursor is restored on both success and failure. */
bool xx_iso_zisofs_extract(xx_io_device *source, int64_t data_offset, uint32_t data_size, const xx_iso_zisofs_info *info, xx_io_device *destination, xx_pd_struct *pd);

#endif /* XX_ISO_ZISOFS_NATIVE_H */
