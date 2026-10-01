/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_idtech_md5anim_extractor.c - search raw data for idtech_md5anim.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the idtech_md5anim reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/idtech_md5anim/xx_idtech_md5anim.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_IDTECH_MD5ANIM };

static Abstractformat *xx_idtech_md5anim_search_open(xx_io_device *window) {
    xx_idtech_md5anim *reader = xx_idtech_md5anim_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_idtech_md5anim_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_idtech_md5anim_free((xx_idtech_md5anim *)format);
}

static const uint8_t anchor_0[] = {0x4d,0x44,0x35,0x56,0x65,0x72,0x73,0x69,0x6f,0x6e};
static const xx_format_search_anchor anchors[] = { { anchor_0,sizeof(anchor_0),0 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_idtech_md5anim_search_open, xx_idtech_md5anim_search_close
};

static xx_format_search_state *xx_idtech_md5anim_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_idtech_md5anim_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_idtech_md5anim_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_idtech_md5anim_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_idtech_md5anim_extractor = {
    xx_idtech_md5anim_create_format_search,
    xx_idtech_md5anim_get_current_format_info,
    xx_idtech_md5anim_format_search_find_next,
    xx_idtech_md5anim_free_format_search
};
