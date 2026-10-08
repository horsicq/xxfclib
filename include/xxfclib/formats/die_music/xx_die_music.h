/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 * DIE music-family adapters. A family reader preserves the encoded source
 * file; formats with native parsers keep their more detailed readers.
 */
#ifndef XX_DIE_MUSIC_H
#define XX_DIE_MUSIC_H

#include "xxfclib/formats/xx_format.h"
#include "xxfclib/io/xx_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_die_music_descriptor {
    const char *format_id;       /* Stable //fmt[...] identifier in audio.1.sg. */
    const char *display_name;    /* Short source-derived hint. */
    const char *source_script;
    unsigned source_line;
    bool heuristic;
    xx_file_type_t file_type;
} xx_die_music_descriptor;

/* Shared implementation used by one C module in each music-family folder. */
XXFC_API Abstractformat *xx_die_music_reader_create(
    const xx_die_music_descriptor *descriptor, xx_io_device *device,
    int64_t base_address);
XXFC_API void xx_die_music_reader_free(void *reader);

/* DIE's original ordered rule is evaluated once for an unknown file. */
XXFC_API xx_file_type_t xx_die_music_detect_device(xx_io_device *device);

#include "xxfclib/formats/die_music/xx_die_music_abstract_extractor_decls.inc"

#ifdef __cplusplus
}
#endif
#endif
