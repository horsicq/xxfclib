/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Read-only Emulator II sample-track wrapper; exact extraction scope is documented in the source.
 */
#ifndef XX_EMULATORII_EII_H
#define XX_EMULATORII_EII_H
#include "xxfclib/formats/hxc_sector/xx_hxc_sector.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef xx_hxc_sector_info xx_emulatorii_eii;
XXFC_API void xx_emulatorii_eii_init(xx_emulatorii_eii *, xx_io_device *, int64_t);
XXFC_API xx_emulatorii_eii *xx_emulatorii_eii_create(xx_io_device *, int64_t);
XXFC_API void xx_emulatorii_eii_destroy(xx_emulatorii_eii *);
XXFC_API void xx_emulatorii_eii_free(xx_emulatorii_eii *);
static inline Abstractformat *xx_emulatorii_eii_to_format(xx_emulatorii_eii *r)
{
    return r ? &r->format : NULL;
}
#ifdef __cplusplus
}
#endif
#endif
