/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_pkm_texture_extractor.c - search raw data for pkm_texture.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the pkm_texture reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pkm_texture/xx_pkm_texture.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PKM_TEXTURE };

static Abstractformat *xx_pkm_texture_search_open(xx_io_device *window) {
    xx_pkm_texture *reader = xx_pkm_texture_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_pkm_texture_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_pkm_texture_free((xx_pkm_texture *)format);
}

static const uint8_t anchor_0[] = {0x50,0x4b,0x4d,0x20,0x31,0x30};
static const uint8_t anchor_1[] = {0x50,0x4b,0x4d,0x20,0x32,0x30};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),0 },{ anchor_1,sizeof(anchor_1),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 2U,
    xx_pkm_texture_search_open, xx_pkm_texture_search_close, false
};

static xx_format_search_state *xx_pkm_texture_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_pkm_texture_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_pkm_texture_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_pkm_texture_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_pkm_texture_extractor = {
    xx_pkm_texture_create_format_search,
    xx_pkm_texture_get_current_format_info,
    xx_pkm_texture_format_search_find_next,
    xx_pkm_texture_free_format_search
};
