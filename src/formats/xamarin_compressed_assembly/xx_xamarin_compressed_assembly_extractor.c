/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_xamarin_compressed_assembly_extractor.c - search raw data for XAMARIN_COMPRESSED_ASSEMBLY.
 *
 * Candidates are the fixed bytes 58414C5A at +0.
 * Each candidate must be accepted by the xamarin_compressed_assembly reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/xamarin_compressed_assembly/xx_xamarin_compressed_assembly.h"


static const uint8_t k_anchor0[] = { 0x58, 0x41, 0x4C, 0x5A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_XAMARIN_COMPRESSED_ASSEMBLY };

static Abstractformat *xx_xamarin_compressed_assembly_search_open(xx_io_device *window) {
    xx_xamarin_compressed_assembly *reader = xx_xamarin_compressed_assembly_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_xamarin_compressed_assembly_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_xamarin_compressed_assembly_free((xx_xamarin_compressed_assembly *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_xamarin_compressed_assembly_search_open, xx_xamarin_compressed_assembly_search_close
};

static xx_format_search_state *xx_xamarin_compressed_assembly_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_xamarin_compressed_assembly_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_xamarin_compressed_assembly_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_xamarin_compressed_assembly_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_xamarin_compressed_assembly_extractor = {
    xx_xamarin_compressed_assembly_create_format_search,
    xx_xamarin_compressed_assembly_get_current_format_info,
    xx_xamarin_compressed_assembly_format_search_find_next,
    xx_xamarin_compressed_assembly_free_format_search
};
