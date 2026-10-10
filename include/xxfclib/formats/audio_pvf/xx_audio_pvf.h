/* SPDX-License-Identifier: MIT. Portable Voice Format PVF1/PVF2 reader. */
#ifndef XX_AUDIO_PVF_H
#define XX_AUDIO_PVF_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_audio_pvf {
    Abstractformat format;
} xx_audio_pvf;
XXFC_API void xx_audio_pvf_init(xx_audio_pvf *, xx_io_device *, int64_t);
XXFC_API xx_audio_pvf *xx_audio_pvf_create(xx_io_device *, int64_t);
XXFC_API void xx_audio_pvf_destroy(xx_audio_pvf *);
XXFC_API void xx_audio_pvf_free(xx_audio_pvf *);
XXFC_API bool xx_audio_pvf_check_is_valid(Abstractformat *, xx_pd_struct *);
XXFC_API bool xx_audio_pvf_handle_base_info(Abstractformat *, xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_audio_pvf_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API xx_file_type_t xx_audio_pvf_file_type(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_audio_pvf_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_audio_pvf_get_abstract_extractor(void);
XXFC_API Abstractdetector *xx_audio_pvf_get_abstract_detector(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
