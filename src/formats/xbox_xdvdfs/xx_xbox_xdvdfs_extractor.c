/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_xbox_xdvdfs_extractor.c - search raw data for xbox_xdvdfs.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the xbox_xdvdfs reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/xbox_xdvdfs/xx_xbox_xdvdfs.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_XBOX_XDVDFS };

static Abstractformat *xx_xbox_xdvdfs_search_open(xx_io_device *window) {
    xx_xbox_xdvdfs *reader = xx_xbox_xdvdfs_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_xbox_xdvdfs_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_xbox_xdvdfs_free((xx_xbox_xdvdfs *)format);
}

static const uint8_t anchor_bytes[] = {0x4d,0x49,0x43,0x52,0x4f,0x53,0x4f,0x46,0x54,0x2a,0x58,0x42,0x4f,0x58,0x2a,0x4d,0x45,0x44,0x49,0x41};
static const xx_format_search_anchor anchors[] = { { anchor_bytes,sizeof(anchor_bytes),65536 } };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_xbox_xdvdfs_search_open, xx_xbox_xdvdfs_search_close, false
};

static xx_format_search_state *xx_xbox_xdvdfs_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_xbox_xdvdfs_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_xbox_xdvdfs_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_xbox_xdvdfs_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_xbox_xdvdfs_extractor = {
    xx_xbox_xdvdfs_create_format_search,
    xx_xbox_xdvdfs_get_current_format_info,
    xx_xbox_xdvdfs_format_search_find_next,
    xx_xbox_xdvdfs_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(xbox_xdvdfs, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
