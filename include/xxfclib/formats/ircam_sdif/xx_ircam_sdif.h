/* SPDX-License-Identifier: MIT
 * Wire specification: https://sdif.sourceforge.net/standard/sdif-standard.html */
#ifndef XX_IRCAM_SDIF_H
#define XX_IRCAM_SDIF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_ircam_sdif { Abstractformat format; } xx_ircam_sdif;
XXFC_API void xx_ircam_sdif_init(xx_ircam_sdif *,xx_io_device *,int64_t);
XXFC_API xx_ircam_sdif *xx_ircam_sdif_create(xx_io_device *,int64_t);
XXFC_API void xx_ircam_sdif_destroy(xx_ircam_sdif *);
XXFC_API void xx_ircam_sdif_free(xx_ircam_sdif *);
XXFC_API bool xx_ircam_sdif_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_ircam_sdif_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_ircam_sdif_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_ircam_sdif_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_ircam_sdif_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
