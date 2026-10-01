/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_linuxboot_extractor.c - raw-data search for Microsoft LINUXBOOT.
 *
 * Candidates are nominated by a fixed signature; the reader validates the
 * structure and the detector must confirm the type before a find is returned.
 * See xx_format_extractor_engine.h for the shared bounded search engine.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/linuxboot/xx_linuxboot.h"

static const uint8_t k_anchor0[] = { 0xB8, 0xC0, 0x07, 0x8E, 0xD8, 0xB8, 0x00, 0x90, 0x8E, 0xC0, 0xB9, 0x00, 0x01, 0x29, 0xF6, 0x29 };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_LINUXBOOT };

static Abstractformat *xx_linuxboot_search_open(xx_io_device *window) {
    xx_linuxboot *reader = xx_linuxboot_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_linuxboot_search_close(Abstractformat *format) {
    xx_linuxboot_free((xx_linuxboot *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_linuxboot_search_open, xx_linuxboot_search_close
};

static xx_format_search_state *xx_linuxboot_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_linuxboot_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_linuxboot_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_linuxboot_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_linuxboot_extractor = {
    xx_linuxboot_create_format_search,
    xx_linuxboot_get_current_format_info,
    xx_linuxboot_format_search_find_next,
    xx_linuxboot_free_format_search
};

