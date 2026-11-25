/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "peazip_backend.h"
#include "upx_backend.h"

#include <stdio.h>

static void xfu_writer_unavailable(const xfu_request *request, const char *format)
{
    char message[192];
    if (!request || !request->callbacks.log) return;
    (void)snprintf(message, sizeof(message), "%s creation is unavailable in the xxfclib-only build", format ? format : "this format");
    request->callbacks.log(request->callbacks.user, true, message);
}

int xfu_peazip_create(const xfu_request *request, const char *format, xx_pd_struct *pd)
{
    (void)pd;
    xfu_writer_unavailable(request, format);
    return 2;
}

int xfu_upx_create(const xfu_request *request, xx_pd_struct *pd)
{
    (void)pd;
    xfu_writer_unavailable(request, "UPX");
    return 2;
}
