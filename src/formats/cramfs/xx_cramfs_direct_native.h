/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XX_CRAMFS_DIRECT_NATIVE_H
#define XX_CRAMFS_DIRECT_NATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Locate an official mkcramfs -X uncompressed direct block. Pointer offsets
 * are relative to the cramfs superblock and count four-byte units. The two
 * high bits identify direct and uncompressed storage. Compressed direct
 * pointers require a length prefix and are intentionally excluded here. */
bool xx_cramfs_direct_native_span(uint32_t pointer, int64_t base,
                                  int64_t image_end, size_t expected,
                                  int64_t *start, int64_t *next);

#endif
