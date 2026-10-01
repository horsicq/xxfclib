/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Search raw data for InstallShield ISSetupStream.
 * The reader validates and measures each candidate, and the detector must
 * name the same format on the candidate view. Offset 0 is always tried.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/installshield_issetupstream/xx_installshield_issetupstream.h"

static const uint8_t k_anchor0[] = { 0x4D, 0x5A };
static const uint8_t k_anchor1[] = { 0x49, 0x53, 0x53, 0x65, 0x74, 0x75, 0x70, 0x53, 0x74, 0x72, 0x65, 0x61, 0x6D, 0x00 };
static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
    { k_anchor1, sizeof(k_anchor1), 0U },
};

static const xx_file_type_t k_types[] = {
    XX_FILE_TYPE_INSTALLSHIELD_ISSETUPSTREAM
};

static Abstractformat *xx_installshield_issetupstream_search_open(xx_io_device *window) {
    xx_installshield_issetupstream *reader = xx_installshield_issetupstream_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_installshield_issetupstream_search_close(Abstractformat *format) {
    xx_installshield_issetupstream_free((xx_installshield_issetupstream *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_installshield_issetupstream_search_open, xx_installshield_issetupstream_search_close
};

static xx_format_search_state *xx_installshield_issetupstream_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_installshield_issetupstream_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_installshield_issetupstream_format_search_find_next(
    xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_installshield_issetupstream_free_format_search(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_installshield_issetupstream_extractor = {
    xx_installshield_issetupstream_create_format_search,
    xx_installshield_issetupstream_get_current_format_info,
    xx_installshield_issetupstream_format_search_find_next,
    xx_installshield_issetupstream_free_format_search
};
