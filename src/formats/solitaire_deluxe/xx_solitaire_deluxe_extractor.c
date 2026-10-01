/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */
/* The banner is an anchor only; the reader validates the prologue, bounded
 * directory, and every complete DCL member before accepting a candidate. */
#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/solitaire_deluxe/xx_solitaire_deluxe.h"
#ifdef SOLITAIRE_DELUXE
#define SD_TYPE XX_FILE_TYPE_SOLITAIRE_DELUXE
#else
#define SD_TYPE XX_FILE_TYPE_UNKNOWN
#endif

static const uint8_t sd_banner[] = {
    'S','o','l','i','t','a','i','r','e',' ',
    'D','e','l','u','x','e','.'
};
static const xx_format_search_anchor sd_anchors[] = {
    { sd_banner, sizeof(sd_banner), 0U }
};
static const xx_file_type_t sd_types[] = {
    SD_TYPE
};
static Abstractformat *sd_open(xx_io_device *window) {
    xx_solitaire_deluxe *reader = xx_solitaire_deluxe_create(window, 0);
    return reader ? &reader->format : NULL;
}
static void sd_close(Abstractformat *format) {
    xx_solitaire_deluxe_free((xx_solitaire_deluxe *)format);
}
static const xx_format_search_desc sd_search = {
    sd_types, sizeof(sd_types) / sizeof(sd_types[0]),
    sd_anchors, sizeof(sd_anchors) / sizeof(sd_anchors[0]),
    sd_open, sd_close
};
static xx_format_search_state *sd_create_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&sd_search, device, options, pd);
}
static const xx_format_search_info *sd_current(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}
static bool sd_next(xx_format_extractor *self,
                    xx_format_search_state *state, xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}
static void sd_free_search(xx_format_extractor *self,
                           xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}
xx_format_extractor xx_solitaire_deluxe_extractor = {
    sd_create_search, sd_current, sd_next, sd_free_search
};
