/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * https://midi.org/standard-midi-files
 * Publishes stored payload components; see docs/registered_second_fifty_formats.md.
 */
#ifndef XX_MIDI_H
#define XX_MIDI_H
#include "xxfclib/formats/xx_format.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct xx_midi { Abstractformat format; } xx_midi;
XXFC_API void xx_midi_init(xx_midi *,xx_io_device *,int64_t);
XXFC_API xx_midi *xx_midi_create(xx_io_device *,int64_t);
XXFC_API void xx_midi_destroy(xx_midi *);
XXFC_API void xx_midi_free(xx_midi *);
XXFC_API bool xx_midi_check_is_valid(Abstractformat *,xx_pd_struct *);
XXFC_API bool xx_midi_handle_base_info(Abstractformat *,xx_pd_struct *);
#ifdef __cplusplus
}
#endif
#endif
