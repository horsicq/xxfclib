/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_format_extractor.c - the shared raw-data search, and the lookup from a
 * file type to its format's search. See xx_format_extractor_engine.h for how
 * a search decides what counts as a find. */

#include "xx_format_extractor_engine.h"

#include "xxfclib/data/xx_pd.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/memory/xx_memory.h"
#include "xxfclib/rt/xx_rt.h"

/* Bytes scanned per refill. The buffer is this plus the deepest anchor, so a
 * candidate near the end of one chunk is still checked in full. */
#define XX_FORMAT_SEARCH_CHUNK (64U * 1024U)

struct xx_format_search_state {
    const xx_format_search_desc *desc;
    xx_io_device *device;
    int64_t total;

    uint8_t *buffer;
    size_t buffer_cap;
    int64_t buffer_start; /* device offset of buffer[0] */
    size_t buffer_len;    /* valid bytes in buffer */
    size_t lookahead;     /* deepest anchor end, from a candidate start */

    int64_t next;         /* first candidate start not yet tested */
    bool done;
    bool has_current;
    xx_format_search_info current;
};

static bool xx_format_search_type_matches(const xx_format_search_desc *desc,
                                          xx_file_type_t type) {
    size_t i;
    for (i = 0; i < desc->type_count; ++i) {
        if (desc->types[i] == type) return true;
    }
    return false;
}

/* Test one candidate start. On a find, sets the current info and moves the
 * scan past it.
 *
 * The format's own reader looks first: it turns most chance anchor matches
 * away after reading a header, where the detector would run every late
 * structural probe it has over the rest of the device. Only a candidate the
 * reader accepts goes on to the detector. A find therefore needs both: the
 * reader to accept it (so it can be read) and the detector to name one of
 * the format's types (so it is what detection would call it). */
static bool xx_format_search_test(xx_format_search_state *state, int64_t start,
                                  xx_pd_struct *pd) {
    xx_io_volume volume;
    xx_io_device *window;
    xx_file_type_t type;
    Abstractformat *format;
    int64_t size = -1;

    if (start < 0 || start >= state->total) return false;
    volume.device = state->device;
    volume.offset = start;
    volume.size = state->total - start;
    window = xx_io_multivolume_open(&volume, 1U, false);
    if (!window) return false;

    format = state->desc->open(window);
    if (!format) {
        xx_io_close(window);
        return false;
    }
    if (!xx_format_is_valid(format, pd)) {
        state->desc->close(format);
        xx_io_close(window);
        return false;
    }
    if (xx_format_handle_base_info(format, pd)) {
        int64_t measured = xx_format_get_format_size(format, pd);
        /* A size the view cannot hold is not a size. */
        if (measured > 0 && measured <= volume.size) size = measured;
    }
    state->desc->close(format);

    type = xx_format_get_file_type_device(window);
    xx_io_close(window);
    if (!xx_format_search_type_matches(state->desc, type)) return false;

    state->current.file_type = type;
    state->current.offset = start;
    state->current.size = size;
    state->has_current = true;
    state->next = start + (size > 0 ? size : 1);
    return true;
}

/* Load the buffer so that it starts at state->next. */
static bool xx_format_search_refill(xx_format_search_state *state) {
    size_t got = 0;

    if (xx_io_seek64(state->device, state->next, SEEK_SET) != 0) return false;
    while (got < state->buffer_cap) {
        ssize_t n = xx_io_read(state->device, state->buffer + got,
                               state->buffer_cap - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    state->buffer_start = state->next;
    state->buffer_len = got;
    return got != 0;
}

static bool xx_format_search_advance(xx_format_search_state *state,
                                     xx_pd_struct *pd) {
    const xx_format_search_desc *desc = state->desc;

    state->has_current = false;
    if (state->done) return false;

    if (desc->anchor_count == 0U) {
        /* No signature to scan for: the format can only be recognised where
         * the detector would recognise it, at the start. */
        state->done = true;
        return state->next == 0 && xx_format_search_test(state, 0, pd);
    }

    while (state->next < state->total) {
        int64_t buffer_end;
        int64_t limit;
        int64_t s;

        if (pd && xx_pd_is_stopped(pd)) break;

        buffer_end = state->buffer_start + (int64_t)state->buffer_len;
        if (state->next < state->buffer_start ||
            (buffer_end < state->total &&
             state->next + (int64_t)state->lookahead > buffer_end)) {
            if (!xx_format_search_refill(state)) break;
            buffer_end = state->buffer_start + (int64_t)state->buffer_len;
        }
        /* Every start below `limit` has all its anchor bytes in the buffer,
         * or would run past the end of the device. */
        limit = buffer_end >= state->total
                    ? state->total
                    : buffer_end - (int64_t)state->lookahead + 1;
        /* A full refill always moves the limit forward. One that cannot means
         * the device stopped delivering bytes before its reported end. */
        if (limit <= state->next) break;

        for (s = state->next; s < limit; ++s) {
            size_t a;
            bool candidate = false;
            for (a = 0; a < desc->anchor_count && !candidate; ++a) {
                const xx_format_search_anchor *anchor = &desc->anchors[a];
                int64_t at = s + (int64_t)anchor->offset - state->buffer_start;
                if (at + (int64_t)anchor->size > (int64_t)state->buffer_len) continue;
                candidate = state->buffer[at] == anchor->bytes[0] &&
                            xx_rt_memcmp(state->buffer + at, anchor->bytes,
                                         anchor->size) == 0;
            }
            /* Offset 0 is always a candidate: it is where the detector looks,
             * so a search never finds less than detection does, even for a
             * variant of the format that has no anchor. */
            if ((candidate || s == 0) && xx_format_search_test(state, s, pd))
                return true;
        }
        state->next = limit;
    }
    state->done = true;
    return false;
}

xx_format_search_state *xx_format_search_create(const xx_format_search_desc *desc,
                                                xx_io_device *device,
                                                const xx_list_s *options,
                                                xx_pd_struct *pd) {
    xx_format_search_state *state;
    size_t i;

    (void)options; /* reserved */
    if (!desc || !device || desc->type_count == 0U || !desc->open || !desc->close)
        return NULL;

    state = (xx_format_search_state *)xx_mem_calloc(1U, sizeof(*state));
    if (!state) return NULL;
    state->desc = desc;
    state->device = device;
    state->total = xx_io_total_size(device);
    if (state->total < 0) state->total = 0;

    for (i = 0; i < desc->anchor_count; ++i) {
        size_t end = (size_t)desc->anchors[i].offset + desc->anchors[i].size;
        if (desc->anchors[i].size == 0U || end > XX_FORMAT_SEARCH_MAX_LOOKAHEAD) {
            xx_mem_free(state);
            return NULL;
        }
        if (end > state->lookahead) state->lookahead = end;
    }
    if (desc->anchor_count != 0U) {
        state->buffer_cap = XX_FORMAT_SEARCH_CHUNK + state->lookahead;
        state->buffer = (uint8_t *)xx_mem_alloc(state->buffer_cap);
        if (!state->buffer) {
            xx_mem_free(state);
            return NULL;
        }
        /* Empty buffer positioned before the device: the first advance fills it. */
        state->buffer_start = -1;
    }

    (void)xx_format_search_advance(state, pd);
    return state;
}

const xx_format_search_info *xx_format_search_current(xx_format_search_state *state) {
    return (state && state->has_current) ? &state->current : NULL;
}

bool xx_format_search_find_next(xx_format_search_state *state, xx_pd_struct *pd) {
    if (!state) return false;
    return xx_format_search_advance(state, pd);
}

void xx_format_search_free(xx_format_search_state *state) {
    if (!state) return;
    xx_mem_free(state->buffer);
    xx_mem_free(state);
}

/* ------------------------------------------------------------------------ */
/* One extractor per format, and the file types each answers to. Generated. */

#include "xx_format_extractor_list.inc"

xx_format_extractor *xx_format_extractor_get(xx_file_type_t type) {
    size_t i;
    for (i = 0; i < sizeof(g_xx_format_extractors) / sizeof(g_xx_format_extractors[0]); ++i) {
        if (g_xx_format_extractors[i].type == type) return g_xx_format_extractors[i].extractor;
    }
    return NULL;
}

size_t xx_format_extractor_count(void) {
    return sizeof(g_xx_format_extractors) / sizeof(g_xx_format_extractors[0]);
}

xx_format_extractor *xx_format_extractor_at(size_t index, xx_file_type_t *type) {
    if (index >= xx_format_extractor_count()) return NULL;
    if (type) *type = g_xx_format_extractors[index].type;
    return g_xx_format_extractors[index].extractor;
}
