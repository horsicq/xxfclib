/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search raw data for Installer VISE for Windows.
 * The reader validates and measures each candidate, and the detector must
 * name the same format on the candidate view. Offset 0 is always tried.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/installer_vise_windows/xx_installer_vise_windows.h"

static const uint8_t k_anchor0[] = { 0x4D, 0x5A };
static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = {
    XX_FILE_TYPE_INSTALLER_VISE_WINDOWS
};

static Abstractformat *xx_installer_vise_windows_search_open(xx_io_device *window) {
    xx_installer_vise_windows *reader = xx_installer_vise_windows_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_installer_vise_windows_search_close(Abstractformat *format) {
    xx_installer_vise_windows_free((xx_installer_vise_windows *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_installer_vise_windows_search_open, xx_installer_vise_windows_search_close, false
};

static xx_format_search_state *xx_installer_vise_windows_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_installer_vise_windows_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_installer_vise_windows_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_installer_vise_windows_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_installer_vise_windows_extractor = {
    xx_installer_vise_windows_create_format_search,
    xx_installer_vise_windows_get_current_format_info,
    xx_installer_vise_windows_format_search_find_next,
    xx_installer_vise_windows_free_format_search
};
