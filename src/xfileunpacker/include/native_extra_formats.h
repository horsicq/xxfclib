/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFU_NATIVE_EXTRA_FORMATS_H
#define XFU_NATIVE_EXTRA_FORMATS_H

#include "core.h"
#include "native_writer_backend.h"
#include <xxfclib/io/xx_io.h>

/* The caller owns source validation, staging, outer filters and publication. */
bool xfu_native_extra_can_create(const char *format);
/* Whether the format has folder members, and whether it stores only UTF-8
 * member names. */
bool xfu_native_extra_stores_folders(const char *format);
bool xfu_native_extra_needs_utf8(const char *format);
/* members: one for each of request->files, as xfu_native_create walked them;
 * their metadata goes to the records. */
bool xfu_native_extra_write(const xfu_request *request, const char *format, const xfu_native_member *members, xx_io_device *output, xx_pd_struct *pd);

#endif
