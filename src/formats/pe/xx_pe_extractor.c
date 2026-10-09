/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_pe_extractor.c - search raw data for PE32 and PE64.
 *
 * Scans for:
 *   4D 5A at +0  ("MZ")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the pe reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * The Abstractextractor below also exposes PE detection and size callbacks.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pe/xx_pe.h"

static const uint8_t k_anchor0[] = { 0x4D, 0x5A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PE32, XX_FILE_TYPE_PE64, XX_FILE_TYPE_DOTNET };

static int64_t xx_pe_search_size(Abstractformat *format, xx_pd_struct *pd) {
    (void)pd;
    return xx_pe_size(format->device, format->base_address, format->is_mapped);
}

static Abstractformat *xx_pe_search_open(xx_io_device *window) {
    xx_pe *reader = xx_pe_create(window, 0);
    if (reader) reader->format.get_format_size = xx_pe_search_size;
    return reader ? &reader->format : NULL;
}

static void xx_pe_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_pe_free((xx_pe *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_pe_search_open, xx_pe_search_close, false
};

static xx_format_search_state *xx_pe_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_pe_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_pe_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_pe_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_pe_extractor = {
    xx_pe_create_format_search,
    xx_pe_get_current_format_info,
    xx_pe_format_search_find_next,
    xx_pe_free_format_search
};

static xx_format_search_state *xx_pe_abstract_create_format_search(
    Abstractextractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_pe_abstract_get_current_format_info(
    Abstractextractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_pe_abstract_format_search_find_next(
    Abstractextractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_pe_abstract_free_format_search(
    Abstractextractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

static Abstractextractor xx_pe_abstract_extractor = {
    .file_type = xx_pe_file_type,
    .fast_detect = xx_pe_fast_detect,
    .size = xx_pe_size,
    .create_format_search = xx_pe_abstract_create_format_search,
    .get_current_format_info = xx_pe_abstract_get_current_format_info,
    .format_search_find_next = xx_pe_abstract_format_search_find_next,
    .free_format_search = xx_pe_abstract_free_format_search
};

Abstractextractor *xx_pe_get_abstract_extractor(void) {
    return &xx_pe_abstract_extractor;
}

static Abstractdetector xx_pe_abstract_detector = {
    .fast_detect = xx_pe_fast_detect,
    .file_type = xx_pe_file_type
};

Abstractdetector *xx_pe_get_abstract_detector(void) {
    return &xx_pe_abstract_detector;
}
