/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_pyinstaller_one_executable_extractor.c - search raw data for PyInstaller CArchive.
 *
 * No invariant prefix identifies this format's start, so only offset 0
 * is tried. Footer-located and headerless streams still use their reader's
 * full structural validation.
 * Each candidate must be accepted by the pyinstaller_one_executable reader, which also measures it,
 * and named by the detector, both on a view that starts at the candidate.
 * See xx_format_extractor_engine.h.
 */

#include "../xx_format_extractor_engine.h"
#include "xxfclib/formats/pyinstaller_one_executable/xx_pyinstaller_one_executable.h"

static const xx_file_type_t k_types[] = { XX_FILE_TYPE_PYINSTALLER_ONE_EXECUTABLE };

static Abstractformat *xx_pyinstaller_one_executable_search_open(xx_io_device *window) {
    xx_pyinstaller_one_executable *reader = xx_pyinstaller_one_executable_create(window, 0);
    return reader ? &reader->format : NULL;
}

static void xx_pyinstaller_one_executable_search_close(Abstractformat *format) {
    /* The format is the first member, so this is the reader itself. */
    xx_pyinstaller_one_executable_free((xx_pyinstaller_one_executable *)format);
}

static const xx_format_search_desc k_desc = {
    k_types, sizeof(k_types) / sizeof(k_types[0]),
    NULL, 0U,
    xx_pyinstaller_one_executable_search_open, xx_pyinstaller_one_executable_search_close
};

static xx_format_search_state *xx_pyinstaller_one_executable_create_format_search(
    xx_format_extractor *self, xx_io_device *device, const xx_list_s *options,
    xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_create(&k_desc, device, options, pd);
}

static const xx_format_search_info *xx_pyinstaller_one_executable_get_current_format_info(
    xx_format_extractor *self, xx_format_search_state *state) {
    (void)self;
    return xx_format_search_current(state);
}

static bool xx_pyinstaller_one_executable_format_search_find_next(xx_format_extractor *self,
                                          xx_format_search_state *state,
                                          xx_pd_struct *pd) {
    (void)self;
    return xx_format_search_find_next(state, pd);
}

static void xx_pyinstaller_one_executable_free_format_search(xx_format_extractor *self,
                                     xx_format_search_state *state) {
    (void)self;
    xx_format_search_free(state);
}

xx_format_extractor xx_pyinstaller_one_executable_extractor = {
    xx_pyinstaller_one_executable_create_format_search,
    xx_pyinstaller_one_executable_get_current_format_info,
    xx_pyinstaller_one_executable_format_search_find_next,
    xx_pyinstaller_one_executable_free_format_search
};
