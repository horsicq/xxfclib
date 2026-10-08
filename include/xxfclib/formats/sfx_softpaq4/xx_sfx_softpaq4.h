/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/** @file xx_sfx_softpaq4.h @brief Compaq SoftPaq 4 self-extractor reader. */
#ifndef XXFCLIB_FORMAT_SFX_SOFTPAQ4_H
#define XXFCLIB_FORMAT_SFX_SOFTPAQ4_H

#include "xxfclib/formats/xx_format.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The DOS executable ends in a 64-byte "!3PS" descriptor followed by
 * length-prefixed stored members. Descriptor size/offset pairs must tile the
 * entire payload. The ZIP member is exported intact, as SoftPaq does.
 */
typedef struct xx_sfx_softpaq4 {
    Abstractformat format;
} xx_sfx_softpaq4;

XXFC_API void xx_sfx_softpaq4_init(xx_sfx_softpaq4 *, xx_io_device *, int64_t);
XXFC_API xx_sfx_softpaq4 *xx_sfx_softpaq4_create(xx_io_device *, int64_t);
XXFC_API void xx_sfx_softpaq4_destroy(xx_sfx_softpaq4 *);
XXFC_API void xx_sfx_softpaq4_free(xx_sfx_softpaq4 *);
XXFC_API bool xx_sfx_softpaq4_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_sfx_softpaq4_handle_base_info(Abstractformat *, xx_pd_struct *);

#ifdef __cplusplus
}
#endif

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_sfx_softpaq4_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_sfx_softpaq4_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_sfx_softpaq4_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XXFCLIB_FORMAT_SFX_SOFTPAQ4_H */
