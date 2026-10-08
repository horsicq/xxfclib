/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: ../XArchive/sfx/xsfx.cpp
 * Independent bounded parser; borrowed source device; safe numbered outputs.
 */
#ifndef XX_SFX_LHA_H
#define XX_SFX_LHA_H
#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/lha/xx_lha.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_sfx_lha {
    Abstractformat format;
    xx_lha inner;
    int64_t payload_offset;
    /* The Atari LArc stub may append a five-byte trailer after one member.
     * A bounded read-only view keeps the generic LHA parser strict. */
    xx_io_device *bounded_device;
    uint8_t *bounded_bytes;
    bool inner_ready;
    bool checked;
} xx_sfx_lha;
XXFC_API void xx_sfx_lha_init(xx_sfx_lha *,xx_io_device *,int64_t);
XXFC_API xx_sfx_lha *xx_sfx_lha_create(xx_io_device *,int64_t);
XXFC_API void xx_sfx_lha_destroy(xx_sfx_lha *);
XXFC_API void xx_sfx_lha_free(xx_sfx_lha *);
XXFC_API bool xx_sfx_lha_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_sfx_lha_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_lha_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_lha_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_lha_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
