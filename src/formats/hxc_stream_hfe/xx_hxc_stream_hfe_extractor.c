/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_hxc_stream_hfe_extractor.c - search raw data for HXC_STREAM_HFE.
 *
 * Candidates are the fixed bytes 4878435F53747265616D5F496D616765 at +0.
 * Each candidate must be accepted by the hxc_stream_hfe reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/hxc_stream_hfe/xx_hxc_stream_hfe.h"


static const uint8_t k_anchor0[] = { 0x48, 0x78, 0x43, 0x5F, 0x53, 0x74, 0x72, 0x65, 0x61, 0x6D, 0x5F, 0x49, 0x6D, 0x61, 0x67, 0x65 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_HXC_STREAM_HFE };

static Abstractformat *xx_hxc_stream_hfe_search_open(xx_io_device *window) {
    xx_hxc_stream_hfe *reader = xx_hxc_stream_hfe_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_hxc_stream_hfe_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_hxc_stream_hfe_free((xx_hxc_stream_hfe *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_hxc_stream_hfe_search_open, xx_hxc_stream_hfe_search_close, false
};

static xx_format_search_state *xx_hxc_stream_hfe_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_hxc_stream_hfe_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_hxc_stream_hfe_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_hxc_stream_hfe_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_hxc_stream_hfe_extractor = {
    xx_hxc_stream_hfe_create_format_search,
    xx_hxc_stream_hfe_get_current_format_info,
    xx_hxc_stream_hfe_format_search_find_next,
    xx_hxc_stream_hfe_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(hxc_stream_hfe, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
