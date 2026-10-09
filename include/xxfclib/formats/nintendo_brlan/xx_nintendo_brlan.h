/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Layout reference: https://raw.githubusercontent.com/magcius/noclip.website/main/src/Common/NW4R/lyt/Layout.ts
 * Big-endian RLAN version8, one pai1 block with1-32 pane bindings, one RLPA Hermite group each and1-10 tracks. Checks disjoint metadata/key tables, finite ordered keyframes and channel IDs. Exports encoded pai1 component; texture/material/visibility animation, other curve types and playback unsupported.
 */
#ifndef XX_NINTENDO_BRLAN_H
#define XX_NINTENDO_BRLAN_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_nintendo_brlan { Abstractformat format; } xx_nintendo_brlan;
XXFC_API void xx_nintendo_brlan_init(xx_nintendo_brlan *,xx_io_device *,int64_t);
XXFC_API xx_nintendo_brlan *xx_nintendo_brlan_create(xx_io_device *,int64_t);
XXFC_API void xx_nintendo_brlan_destroy(xx_nintendo_brlan *);
XXFC_API void xx_nintendo_brlan_free(xx_nintendo_brlan *);
XXFC_API bool xx_nintendo_brlan_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_nintendo_brlan_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_nintendo_brlan_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_nintendo_brlan_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_nintendo_brlan_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_nintendo_brlan_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_nintendo_brlan_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
