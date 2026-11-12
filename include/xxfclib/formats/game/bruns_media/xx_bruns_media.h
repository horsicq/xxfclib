/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * BRUNS_MEDIA: the family header of the bruns_media type, served by the shared reader
 * declared in game/ue2_games/xx_ue2_games.h.
 */
#ifndef XX_BRUNS_MEDIA_FAMILY_H
#define XX_BRUNS_MEDIA_FAMILY_H
#include "xxfclib/formats/game/ue2_games/xx_ue2_games.h"
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_bruns_media_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_bruns_media_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_bruns_media_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_bruns_media_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_bruns_media_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif /* XX_BRUNS_MEDIA_FAMILY_H */
