/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * FALLOUT_DAT: the family header of the fallout_dat type, served by the shared reader
 * declared in game/ue2_games/xx_ue2_games.h.
 */
#ifndef XX_FALLOUT_DAT_FAMILY_H
#define XX_FALLOUT_DAT_FAMILY_H
#include "xxfclib/formats/game/ue2_games/xx_ue2_games.h"
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_fallout_dat_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_fallout_dat_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_fallout_dat_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_fallout_dat_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_fallout_dat_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XX_FALLOUT_DAT_FAMILY_H */
