/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/DarklightGames/psk_psa_py/master/src/psk_psa_py/psk/writer.py
 * Classic ActorX PSK with ordered PNTS0000/VTXW0000/FACE0000/MATT0000/REFSKELT/RAWWEIGHTS tables, up to65536 points/wedges/faces and256 bones/materials. Checks finite
 * geometry, material/weight/index references and bone hierarchy counts. Exports encoded tables; extended PSK chunks,32-bit face variants, external textures and rendering
 * unsupported.
 */
#ifndef XX_UNREAL_PSK_H
#define XX_UNREAL_PSK_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_unreal_psk {
    Abstractformat format;
} xx_unreal_psk;
XXFC_API void xx_unreal_psk_init(xx_unreal_psk *, xx_io_device *, int64_t);
XXFC_API xx_unreal_psk *xx_unreal_psk_create(xx_io_device *, int64_t);
XXFC_API void xx_unreal_psk_destroy(xx_unreal_psk *);
XXFC_API void xx_unreal_psk_free(xx_unreal_psk *);
XXFC_API bool xx_unreal_psk_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_unreal_psk_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_unreal_psk_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_unreal_psk_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_unreal_psk_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_unreal_psk_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_unreal_psk_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
