/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_renpy_rpa_extractor.c - search raw data for the Ren'Py RPA family.
 *
 * RPA 2.0, 3.0, 3.2, 4.0 and ALT 1.0 share the public RENPY_RPA type.
 * Use the complete RPA reader for validation, extent and metadata, as the
 * archive-opening registry does. The older renpy_rpa reader only understands
 * a restricted RPA 3.0 index grammar.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/renpy_rpa/xx_renpy_rpa.h"
#include <xxfclib/formats/rpa/xx_rpa.h>

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_RENPY_RPA };

static Abstractformat *xx_renpy_rpa_search_open(xx_io_device *window) {
    xx_rpa *reader = xx_rpa_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_renpy_rpa_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_rpa_free((xx_rpa *)format);
}

static const uint8_t anchor_rpa20[] = "RPA-2.0 ";
static const uint8_t anchor_rpa30[] = "RPA-3.0 ";
static const uint8_t anchor_rpa32[] = "RPA-3.2 ";
static const uint8_t anchor_rpa40[] = "RPA-4.0 ";
static const uint8_t anchor_alt10[] = "ALT-1.0 ";
static const xx_format_search_anchor anchors[] = {
    { anchor_rpa20, sizeof(anchor_rpa20) - 1U, 0U },
    { anchor_rpa30, sizeof(anchor_rpa30) - 1U, 0U },
    { anchor_rpa32, sizeof(anchor_rpa32) - 1U, 0U },
    { anchor_rpa40, sizeof(anchor_rpa40) - 1U, 0U },
    { anchor_alt10, sizeof(anchor_alt10) - 1U, 0U }
};

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, sizeof(anchors) / sizeof(anchors[0]),
    xx_renpy_rpa_search_open, xx_renpy_rpa_search_close, true
};

static xx_format_search_state *xx_renpy_rpa_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_renpy_rpa_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_renpy_rpa_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_renpy_rpa_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_renpy_rpa_extractor = {
    xx_renpy_rpa_create_format_search,
    xx_renpy_rpa_get_current_format_info,
    xx_renpy_rpa_format_search_find_next,
    xx_renpy_rpa_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(renpy_rpa, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
