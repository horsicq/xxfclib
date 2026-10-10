/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_WII_WAD_H
#define XX_WII_WAD_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_wii_wad {
    Abstractformat format;
} xx_wii_wad;
enum {
    XX_WII_WAD_METHOD_AES128_CBC = 1
};
/* v0 Is/ib WADs with RSA2048 ticket and TMD. Metadata needs no key.
 * Content extraction requires OPT_PASSWORD as exactly 16 key bytes or 32 hex
 * digits. Supply the common key matching the ticket's index/console variant.
 * No keys are embedded or loaded from companion files. SHA1 is always checked;
 * this validates content integrity, not the ticket/TMD RSA signatures. */
XXFC_API void xx_wii_wad_init(xx_wii_wad *, xx_io_device *, int64_t);
XXFC_API xx_wii_wad *xx_wii_wad_create(xx_io_device *, int64_t);
XXFC_API void xx_wii_wad_destroy(xx_wii_wad *);
XXFC_API void xx_wii_wad_free(xx_wii_wad *);
XXFC_API xx_file_type_t xx_wii_wad_detect(xx_io_device *, int64_t);
static inline Abstractformat *xx_wii_wad_to_format(xx_wii_wad *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
