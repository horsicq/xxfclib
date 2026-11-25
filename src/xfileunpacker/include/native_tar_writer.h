/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFU_NATIVE_TAR_WRITER_H
#define XFU_NATIVE_TAR_WRITER_H

#include "core.h"
#include "native_writer_backend.h"
#include <xxfclib/io/xx_io.h>

bool xfu_native_tar_can_create(const char *format);
/* members: one for each of request->files, as xfu_native_create walked them;
 * their metadata goes to the headers. */
bool xfu_native_tar_write(const xfu_request *request, const char *format, const xfu_native_member *members, xx_io_device *output, xx_pd_struct *pd);

#endif
