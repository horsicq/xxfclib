/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_gmsh_msh_extractor.c - search raw data for gmsh_msh.
 *
 * Candidates use a fixed signature and the full bounded reader validation.
 * Each candidate must be accepted by the gmsh_msh reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/gmsh_msh/xx_gmsh_msh.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_GMSH_MSH };

static Abstractformat *xx_gmsh_msh_search_open(xx_io_device *window) {
    xx_gmsh_msh *reader = xx_gmsh_msh_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_gmsh_msh_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_gmsh_msh_free((xx_gmsh_msh *)format);
}

static const uint8_t anchor_bytes[] = {0x24,0x4d,0x65,0x73,0x68,0x46,0x6f,0x72,0x6d,0x61,0x74};
static const xx_format_search_anchor anchors[] = { {anchor_bytes,sizeof(anchor_bytes),0} };

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    anchors, 1U,
    xx_gmsh_msh_search_open, xx_gmsh_msh_search_close
};

static xx_format_search_state *xx_gmsh_msh_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_gmsh_msh_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_gmsh_msh_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_gmsh_msh_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_gmsh_msh_extractor = {
    xx_gmsh_msh_create_format_search,
    xx_gmsh_msh_get_current_format_info,
    xx_gmsh_msh_format_search_find_next,
    xx_gmsh_msh_free_format_search
};
