/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * Human Machine Interfaces HMI song: stored header and track records.
 */
#ifndef XX_HMI_MIDI_H
#define XX_HMI_MIDI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_hmi_midi { Abstractformat format; } xx_hmi_midi;
XXFC_API void xx_hmi_midi_init(xx_hmi_midi *,xx_io_device *,int64_t);
XXFC_API xx_hmi_midi *xx_hmi_midi_create(xx_io_device *,int64_t);
XXFC_API void xx_hmi_midi_destroy(xx_hmi_midi *);
XXFC_API void xx_hmi_midi_free(xx_hmi_midi *);
XXFC_API bool xx_hmi_midi_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_hmi_midi_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#ifdef __cplusplus
extern "C" {
#endif
XXFC_API bool xx_hmi_midi_fast_detect(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API int64_t xx_hmi_midi_size(xx_io_device *device, int64_t base_address, bool is_mapped);
XXFC_API Abstractextractor *xx_hmi_midi_get_abstract_extractor(void);
#ifdef __cplusplus
}
#endif
/* END GENERATED ABSTRACT EXTRACTOR */
#endif
