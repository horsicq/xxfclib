/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT */
#ifndef XX_ENIGMA_VIRTUAL_BOX_H
#define XX_ENIGMA_VIRTUAL_BOX_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
/** Enigma Virtual Box modern/legacy virtual filesystem, embedded in PE files
 * or standalone EVB packages. Stored and bounded aPLib compressed members.
 * Does not execute the carrier or reconstruct its original executable.
 * Member decode cap 256 MiB; directory depth64 and one million entries.
 * LIST indexes metadata; TEST decodes into RAM without opening output files. */
typedef struct xx_enigma_virtual_box {
    Abstractformat format;
    void *index;
    uint32_t legacy;
    int64_t container_offset;
} xx_enigma_virtual_box;
XXFC_API void xx_enigma_virtual_box_init(xx_enigma_virtual_box *, xx_io_device *, int64_t);
XXFC_API xx_enigma_virtual_box *xx_enigma_virtual_box_create(xx_io_device *, int64_t);
XXFC_API void xx_enigma_virtual_box_destroy(xx_enigma_virtual_box *);
XXFC_API void xx_enigma_virtual_box_free(xx_enigma_virtual_box *);
#ifdef __cplusplus
}
#endif
#endif
