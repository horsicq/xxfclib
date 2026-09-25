/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_solarispkg_extractor.c - search raw data for SolarisPackage.
 *
 * Scans for:
 *   23 20 50 61 43 6B 41 67 45 20 44 61 54 61 53 74 52 65 41 6D 0A at +0  ("# PaCkAgE DaTaStReAm.")
 * Offset 0 is always tried as well.
 * Each candidate must be accepted by the solarispkg reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/solarispkg/xx_solarispkg.h"

static const uint8_t k_anchor0[] = { 0x23, 0x20, 0x50, 0x61, 0x43, 0x6B, 0x41, 0x67, 0x45, 0x20, 0x44, 0x61, 0x54, 0x61, 0x53, 0x74, 0x52, 0x65, 0x41, 0x6D, 0x0A };

static const xx_format_search_anchor k_anchors[] = {
    { k_anchor0, sizeof(k_anchor0), 0U },
};

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_SOLARISPKG };

static Abstractformat *xx_solarispkg_search_open(xx_io_device *window) {
    xx_solarispkg *reader = xx_solarispkg_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_solarispkg_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_solarispkg_free((xx_solarispkg *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    k_anchors, sizeof(k_anchors) / sizeof(k_anchors[0]),
    xx_solarispkg_search_open, xx_solarispkg_search_close
};

static xx_format_search_state *xx_solarispkg_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_solarispkg_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_solarispkg_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_solarispkg_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_solarispkg_extractor = {
    xx_solarispkg_create_format_search,
    xx_solarispkg_get_current_format_info,
    xx_solarispkg_format_search_find_next,
    xx_solarispkg_free_format_search
};
