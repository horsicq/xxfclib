/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * RPG_MAKER_MV: the family header of the rpg_maker_mv type, served by the shared reader
 * declared in game/ue2_games/xx_ue2_games.h.
 */
#ifndef XX_RPG_MAKER_MV_FAMILY_H
#define XX_RPG_MAKER_MV_FAMILY_H
#include "xxfclib/formats/game/ue2_games/xx_ue2_games.h"
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_rpg_maker_mv_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_rpg_maker_mv_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_rpg_maker_mv_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_rpg_maker_mv_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_rpg_maker_mv_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XX_RPG_MAKER_MV_FAMILY_H */
