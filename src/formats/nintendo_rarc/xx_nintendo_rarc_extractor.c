/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search Nintendo RARC magic, validating the complete member table. */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/nintendo_rarc/xx_nintendo_rarc.h"

static const uint8_t k_anchor0[] = { 0x52, 0x41, 0x52, 0x43 };
static const uint8_t k_anchor1[] = { 0x43, 0x52, 0x41, 0x52 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_NINTENDO_RARC };

static Abstractformat *xx_nintendo_rarc_search_open(xx_io_device *window) {
    xx_nintendo_rarc *reader = xx_nintendo_rarc_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_nintendo_rarc_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_nintendo_rarc_free((xx_nintendo_rarc *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_nintendo_rarc_search_open, xx_nintendo_rarc_search_close
};

static xx_format_search_state *xx_nintendo_rarc_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_nintendo_rarc_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_nintendo_rarc_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_nintendo_rarc_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_nintendo_rarc_extractor = {
    xx_nintendo_rarc_create_format_search,
    xx_nintendo_rarc_get_current_format_info,
    xx_nintendo_rarc_format_search_find_next,
    xx_nintendo_rarc_free_format_search
};
