/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/OpenMW/openmw/master/components/bsa/compressedbsafile.cpp
 * Independent bounded parser; borrowed source device; numbered safe outputs.
 */
#ifndef XX_BETHESDA_BSA_H
#define XX_BETHESDA_BSA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_bethesda_bsa { Abstractformat format; } xx_bethesda_bsa;
XXFC_API void xx_bethesda_bsa_init(xx_bethesda_bsa *,xx_io_device *,int64_t);
XXFC_API xx_bethesda_bsa *xx_bethesda_bsa_create(xx_io_device *,int64_t);
XXFC_API void xx_bethesda_bsa_destroy(xx_bethesda_bsa *);
XXFC_API void xx_bethesda_bsa_free(xx_bethesda_bsa *);
XXFC_API bool xx_bethesda_bsa_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_bethesda_bsa_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
