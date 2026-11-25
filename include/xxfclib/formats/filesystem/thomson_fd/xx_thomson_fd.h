/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_THOMSON_FD_H
#define XX_THOMSON_FD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Headerless Thomson double-sided FD dumps. Their entire first side is
 * followed by their entire second side; the native reader reconstructs a
 * standard cylinder/head-interleaved image. Since there is no magic,
 * selection must be explicit. Supported layouts are Thomson 2s160 and
 * 2s320, as emitted by Greaseweazle's FD image writer. */
typedef struct xx_thomson_fd_s {
    Abstractformat format;
    bool hxc_geometry;
} xx_thomson_fd;

XXFC_API void xx_thomson_fd_init(xx_thomson_fd *, xx_io_device *, int64_t);
XXFC_API xx_thomson_fd *xx_thomson_fd_create(xx_io_device *, int64_t);
/* Explicit HxC geometry: 320KiB=80 cylinders/1 head; 640KiB=80/2.
 * The legacy create/init retain Greaseweazle's 320KiB=40/2 interpretation. */
XXFC_API void xx_thomson_fd_init_hxc(xx_thomson_fd *, xx_io_device *, int64_t);
XXFC_API xx_thomson_fd *xx_thomson_fd_create_hxc(xx_io_device *, int64_t);
XXFC_API void xx_thomson_fd_destroy(xx_thomson_fd *);
XXFC_API void xx_thomson_fd_free(xx_thomson_fd *);

#ifdef __cplusplus
}
#endif
#endif
