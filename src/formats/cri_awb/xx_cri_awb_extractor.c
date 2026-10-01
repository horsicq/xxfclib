/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search CRI AFS2 Wave Bank magic, validating the complete member table. */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/cri_awb/xx_cri_awb.h"

static const uint8_t k_anchor0[] = { 0x41, 0x46, 0x53, 0x32 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_CRI_AWB };

static Abstractformat *xx_cri_awb_search_open(xx_io_device *window) {
    xx_cri_awb *reader = xx_cri_awb_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_cri_awb_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_cri_awb_free((xx_cri_awb *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_cri_awb_search_open, xx_cri_awb_search_close
};

static xx_format_search_state *xx_cri_awb_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_cri_awb_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_cri_awb_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_cri_awb_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_cri_awb_extractor = {
    xx_cri_awb_create_format_search,
    xx_cri_awb_get_current_format_info,
    xx_cri_awb_format_search_find_next,
    xx_cri_awb_free_format_search
};
