/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFILEUNPACKER_METADATA_H
#define XFILEUNPACKER_METADATA_H
#include "core.h"
#include <xxfclib/formats/xx_format.h>

/* Owned display properties; unknown IDs/types remain visible. */
bool xfu_record_properties(const xx_archive_record *record, bool zip, xfu_property **properties, size_t *count);
void xfu_free_properties(xfu_property *properties, size_t count);
#endif
