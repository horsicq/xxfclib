/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search raw data for InstallShield 7 setup.boot.
 * The reader validates and measures each candidate, and the detector must
 * name the same format on the candidate view. Offset 0 is always tried.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/installshield_7_setup2/xx_installshield_7_setup2.h"


static const xx_file_type_t k_types[] = {
    XX_FILE_TYPE_INSTALLSHIELD_7_SETUP2
};

static Abstractformat *xx_installshield_7_setup2_search_open(xx_io_device *window) {
    xx_installshield_7_setup2 *reader = xx_installshield_7_setup2_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_installshield_7_setup2_search_close(Abstractformat *format) {
    xx_installshield_7_setup2_free((xx_installshield_7_setup2 *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_installshield_7_setup2_search_open, xx_installshield_7_setup2_search_close
};

static xx_format_search_state *xx_installshield_7_setup2_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_installshield_7_setup2_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_installshield_7_setup2_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_installshield_7_setup2_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_installshield_7_setup2_extractor = {
    xx_installshield_7_setup2_create_format_search,
    xx_installshield_7_setup2_get_current_format_info,
    xx_installshield_7_setup2_format_search_find_next,
    xx_installshield_7_setup2_free_format_search
};
