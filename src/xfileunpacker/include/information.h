/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFILEUNPACKER_INFORMATION_H
#define XFILEUNPACKER_INFORMATION_H
#include "core.h"

/* Format borrowed display entries without retaining or modifying them.
 * selected == NULL requests archive information and exact-value password
 * groups. Otherwise Password properties remain visible even without advanced
 * metadata. Display values are already escaped by the metadata/UI adapters;
 * their whitespace and punctuation are preserved. Free the returned string.
 * NULL indicates invalid input or an allocation/size failure. */
char *xfu_information_text(const char *archive, const xfu_entry *entries, size_t count, const xfu_entry *selected, bool advanced);
#endif
