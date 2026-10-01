/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_svg_extractor.c - raw-data search for Microsoft SVG.
 *
 * Candidates are nominated by a fixed signature; the reader validates the
 * structure and the detector must confirm the type before a find is returned.
 * See xx_format_extractor_engine.h for the shared bounded search engine.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/svg/xx_svg.h"

static const uint8_t k_anchor0[] = { 0x3C, 0x73, 0x76, 0x67, 0x20 };
static const uint8_t k_anchor1[] = { 0x3C, 0x3F, 0x78, 0x6D, 0x6C };
static const uint8_t k_anchor2[] = { 0x3C, 0x21, 0x2D, 0x2D };
static const uint8_t k_anchor3[] = { 0x3C, 0x21, 0x44, 0x4F, 0x43, 0x54, 0x59, 0x50, 0x45 };
static const uint8_t k_anchor4[] = { 0xEF, 0xBB, 0xBF };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
    { k_anchor2, sizeof(k_anchor2), 0U },
    { k_anchor3, sizeof(k_anchor3), 0U },
    { k_anchor4, sizeof(k_anchor4), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SVG };

static Abstractformat *xx_svg_search_open(xx_io_device *window) {
    xx_svg *reader = xx_svg_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_svg_search_close(Abstractformat *format) {
    xx_svg_free((xx_svg *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_svg_search_open, xx_svg_search_close
};

static xx_format_search_state *xx_svg_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_svg_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_svg_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_svg_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_svg_extractor = {
    xx_svg_create_format_search,
    xx_svg_get_current_format_info,
    xx_svg_format_search_find_next,
    xx_svg_free_format_search
};

