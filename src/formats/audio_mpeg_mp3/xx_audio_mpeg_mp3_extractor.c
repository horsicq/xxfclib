/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_audio_mpeg_mp3_extractor.c - search raw data for audio_mpeg_mp3.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the audio_mpeg_mp3 reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/audio_mpeg_mp3/xx_audio_mpeg_mp3.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_AUDIO_MPEG_MP3 };

static Abstractformat *xx_audio_mpeg_mp3_search_open(xx_io_device *window) {
    xx_audio_mpeg_mp3 *reader = xx_audio_mpeg_mp3_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_audio_mpeg_mp3_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_audio_mpeg_mp3_free((xx_audio_mpeg_mp3 *)format);
}

static const uint8_t anchor_0[] = {0x49,0x44,0x33};
static const uint8_t anchor_1[] = {0xff,0xfb};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),0 },{ anchor_1,sizeof(anchor_1),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 2U,
    xx_audio_mpeg_mp3_search_open, xx_audio_mpeg_mp3_search_close
};

static xx_format_search_state *xx_audio_mpeg_mp3_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_audio_mpeg_mp3_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_audio_mpeg_mp3_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_audio_mpeg_mp3_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_audio_mpeg_mp3_extractor = {
    xx_audio_mpeg_mp3_create_format_search,
    xx_audio_mpeg_mp3_get_current_format_info,
    xx_audio_mpeg_mp3_format_search_find_next,
    xx_audio_mpeg_mp3_free_format_search
};
