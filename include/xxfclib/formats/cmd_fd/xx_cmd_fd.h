/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
#ifndef XX_CMD_FD_H
#define XX_CMD_FD_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/* CMD FD2000/FD4000 D1M, D2M and D4M images use swapped heads within each
 * cylinder. The reader returns one conventional cylinder/head-interleaved
 * image. Densities 1/2/4 select the exact extension and track size; no
 * magic exists, so these variants require explicit named selection. */
typedef struct xx_cmd_fd_s {
    Abstractformat format;
    unsigned density;
} xx_cmd_fd;

XXFC_API void xx_cmd_fd_init(xx_cmd_fd *, xx_io_device *, int64_t, unsigned density);
XXFC_API xx_cmd_fd *xx_cmd_fd_create(xx_io_device *, int64_t, unsigned density);
XXFC_API void xx_cmd_fd_destroy(xx_cmd_fd *);
XXFC_API void xx_cmd_fd_free(xx_cmd_fd *);

#ifdef __cplusplus
}
#endif
#endif
