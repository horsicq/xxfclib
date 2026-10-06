/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search raw data for ARDI OS/2 installer.
 * The reader validates and measures each candidate, and the detector must
 * name the same format on the candidate view. Offset 0 is always tried.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/ardi_installer/xx_ardi_installer.h"

static const uint8_t k_anchor0[] = { 0x4D, 0x5A };
static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = {
    XX_FILE_TYPE_ARDI_INSTALLER
};

static Abstractformat *xx_ardi_installer_search_open(xx_io_device *window) {
    xx_ardi_installer *reader = xx_ardi_installer_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_ardi_installer_search_close(Abstractformat *format) {
    xx_ardi_installer_free((xx_ardi_installer *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_ardi_installer_search_open, xx_ardi_installer_search_close, false
};

static xx_format_search_state *xx_ardi_installer_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_ardi_installer_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_ardi_installer_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_ardi_installer_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_ardi_installer_extractor = {
    xx_ardi_installer_create_format_search,
    xx_ardi_installer_get_current_format_info,
    xx_ardi_installer_format_search_find_next,
    xx_ardi_installer_free_format_search
};
