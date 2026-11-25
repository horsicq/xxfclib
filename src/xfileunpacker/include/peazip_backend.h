/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XFILEUNPACKER_PEAZIP_BACKEND_H
#define XFILEUNPACKER_PEAZIP_BACKEND_H

#include "core.h"

/* Additional in-process writers: Brotli and plaintext ZPAQ. */
int xfu_peazip_create(const xfu_request *request, const char *format, xx_pd_struct *pd);

#endif
