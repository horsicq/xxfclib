/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/DarklightGames/psk_psa_py/master/src/psk_psa_py/psa/writer.py
 * Classic ActorX PSA with BONENAMES/ANIMINFO/ANIMKEYS, up to256 bones,1024 sequences and262144 uncompressed transform keys. Checks hierarchy, finite transforms/rates,
 * sequence frame/bone counts and key bounds. Exports encoded bone/sequence/key tables; scale-key extensions, compression and animation playback unsupported.
 */
#ifndef XX_UNREAL_PSA_H
#define XX_UNREAL_PSA_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_unreal_psa {
    Abstractformat format;
} xx_unreal_psa;
XXFC_API void xx_unreal_psa_init(xx_unreal_psa *, xx_io_device *, int64_t);
XXFC_API xx_unreal_psa *xx_unreal_psa_create(xx_io_device *, int64_t);
XXFC_API void xx_unreal_psa_destroy(xx_unreal_psa *);
XXFC_API void xx_unreal_psa_free(xx_unreal_psa *);
XXFC_API bool xx_unreal_psa_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_unreal_psa_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_unreal_psa_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_unreal_psa_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_unreal_psa_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_unreal_psa_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_unreal_psa_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
