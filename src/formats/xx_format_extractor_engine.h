/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* xx_format_extractor_engine.h - the raw-data search every per-format
 * extractor runs.
 *
 * Internal. Included only by src/formats/xx_format_extractor.c and by the
 * per-format src/formats/<name>/xx_<name>_extractor.c files; nothing here is
 * part of the public API.
 *
 * A per-format file describes its format -- which file types it answers to,
 * which bytes to scan for, and how to open its reader -- and forwards the four
 * xx_format_extractor callbacks here. The engine does the rest:
 *
 *   1. Scan the device for any anchor. A match at device offset p for an
 *      anchor that sits `offset` bytes into the format is a candidate format
 *      start at p - offset.
 *   2. Open the format's reader on a view of the device that begins at the
 *      candidate. If the reader refuses it, that is the end of it: most
 *      chance anchor matches stop here, after a header read.
 *   3. Otherwise ask the reader for its size, and give the detector the same
 *      view. The candidate is a find only if the detector names one of the
 *      format's types -- so a search uses exactly the prefilters, probes and
 *      ordering that xx_format_get_file_type_device() does.
 *   4. Resume the scan after the find, or one byte later when its size is
 *      not known.
 *
 * A format with no anchors has no signature that can be found inside other
 * data; it is looked for at offset 0 only.
 */

#ifndef XX_FORMAT_EXTRACTOR_ENGINE_H
#define XX_FORMAT_EXTRACTOR_ENGINE_H

#include "xxfclib/formats/xx_format.h"
#include "xxfclib/formats/xx_format_extractor.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Bytes that occur a fixed distance into every instance of a format. */
typedef struct xx_format_search_anchor {
    const uint8_t *bytes;
    uint32_t size;
    uint32_t offset; /**< Distance from the start of the format to @p bytes */
} xx_format_search_anchor;

/** Everything the engine needs to know about one format. */
typedef struct xx_format_search_desc {
    const xx_file_type_t *types; /**< Detector types that count as a find */
    size_t type_count;
    const xx_format_search_anchor *anchors; /**< NULL: offset 0 only */
    size_t anchor_count;
    /** Construct the format's reader on @p window, which starts at the
     *  candidate. NULL when it cannot be constructed. Required. */
    Abstractformat *(*open)(xx_io_device *window);
    /** Release what @p open returned. Required. */
    void (*close)(Abstractformat *format);
} xx_format_search_desc;

/** Deepest anchor the engine accepts (offset + size), in bytes. */
#define XX_FORMAT_SEARCH_MAX_LOOKAHEAD (1024U * 1024U)

xx_format_search_state *xx_format_search_create(const xx_format_search_desc *desc,
                                                xx_io_device *device,
                                                const xx_list_s *options,
                                                xx_pd_struct *pd);
const xx_format_search_info *xx_format_search_current(xx_format_search_state *state);
bool xx_format_search_find_next(xx_format_search_state *state, xx_pd_struct *pd);
void xx_format_search_free(xx_format_search_state *state);

#ifdef __cplusplus
}
#endif

#endif /* XX_FORMAT_EXTRACTOR_ENGINE_H */
