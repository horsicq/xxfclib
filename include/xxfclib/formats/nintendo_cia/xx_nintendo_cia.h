/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/d0k3/GodMode9/master/arm9/source/game/cia.h
 * CIA type/version 0 with standard RSA2048/SHA256 TMD and stored unencrypted content. Exports certificate chain, ticket, TMD, included content records and optional metadata; verifies content SHA256. No signature trust verification or title-key decryption.
 */
#ifndef XX_NINTENDO_CIA_H
#define XX_NINTENDO_CIA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_cia { Abstractformat format; } xx_nintendo_cia;
XXFC_API void xx_nintendo_cia_init(xx_nintendo_cia *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_cia *xx_nintendo_cia_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_cia_destroy(xx_nintendo_cia *);
XXFC_API void xx_nintendo_cia_free(xx_nintendo_cia *);
XXFC_API bool xx_nintendo_cia_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_cia_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_cia_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_cia_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_cia_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
