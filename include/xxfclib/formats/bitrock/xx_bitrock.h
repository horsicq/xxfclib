/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_BITROCK_H
#define XX_BITROCK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** BitRock/InstallBuilder CookFS CFS0002 and CFS2.200 containers.
 * Reads raw, Deflate, bzip2 and LZMA-Alone pages with MD5/CRC validation;
 * reconstructs files from page slices and joins ___bitrockBigFile chunks.
 * No installer execution or temporary files. Proprietary encrypted/custom
 * pages and newer CFS0003 containers are rejected. Page/index limit 64 MiB,
 * 65535 pages/members, directory depth 64 and 256 MiB default RAM budget. */
typedef struct xx_bitrock { Abstractformat format;void *index; } xx_bitrock;
XXFC_API void xx_bitrock_init(xx_bitrock *,xx_io_device *,int64_t);
XXFC_API xx_bitrock *xx_bitrock_create(xx_io_device *,int64_t);
XXFC_API void xx_bitrock_destroy(xx_bitrock *);
XXFC_API void xx_bitrock_free(xx_bitrock *);
/** Cheap cursor-preserving prefix/EOF/tail signature gate; callers must still
 * run check_is_valid before reporting BitRock/CookFS as a detected format. */
XXFC_API bool xx_bitrock_has_candidate_device(xx_io_device *,int64_t);
#ifdef __cplusplus
}
#endif
#endif
