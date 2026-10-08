/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search raw data for InstallShield 7 All-in-One Setup.
 * The reader validates and measures each candidate, and the detector must
 * name the same format on the candidate view. Offset 0 is always tried.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/installshield_7_setup/xx_installshield_7_setup.h"

static const uint8_t k_anchor0[] = { 0x4D, 0x5A };
static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = {
    XX_FILE_TYPE_INSTALLSHIELD_7_SETUP
};

static Abstractformat *xx_installshield_7_setup_search_open(xx_io_device *window) {
    xx_installshield_7_setup *reader = xx_installshield_7_setup_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_installshield_7_setup_search_close(Abstractformat *format) {
    xx_installshield_7_setup_free((xx_installshield_7_setup *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_installshield_7_setup_search_open, xx_installshield_7_setup_search_close, false
};

static xx_format_search_state *xx_installshield_7_setup_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_installshield_7_setup_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_installshield_7_setup_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_installshield_7_setup_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_installshield_7_setup_extractor = {
    xx_installshield_7_setup_create_format_search,
    xx_installshield_7_setup_get_current_format_info,
    xx_installshield_7_setup_format_search_find_next,
    xx_installshield_7_setup_free_format_search
};

/* BEGIN GENERATED ABSTRACT EXTRACTOR */
#include "../xx_format_abstract_extractor_adapter.h"
XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(installshield_7_setup, k_desc)
/* END GENERATED ABSTRACT EXTRACTOR */
