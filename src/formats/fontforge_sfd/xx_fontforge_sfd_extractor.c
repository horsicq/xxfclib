/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_fontforge_sfd_extractor.c - search raw data for fontforge_sfd.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the fontforge_sfd reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/fontforge_sfd/xx_fontforge_sfd.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_FONTFORGE_SFD };

static Abstractformat *xx_fontforge_sfd_search_open(xx_io_device *window) {
    xx_fontforge_sfd *reader = xx_fontforge_sfd_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_fontforge_sfd_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_fontforge_sfd_free((xx_fontforge_sfd *)format);
}

static const uint8_t anchor_bytes[] = {0x53,0x70,0x6c,0x69,0x6e,0x65,0x46,0x6f,0x6e,0x74,0x44,0x42,0x3a};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_fontforge_sfd_search_open, xx_fontforge_sfd_search_close, false
};

static xx_format_search_state *xx_fontforge_sfd_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_fontforge_sfd_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_fontforge_sfd_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_fontforge_sfd_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_fontforge_sfd_extractor = {
    xx_fontforge_sfd_create_format_search,
    xx_fontforge_sfd_get_current_format_info,
    xx_fontforge_sfd_format_search_find_next,
    xx_fontforge_sfd_free_format_search
};
