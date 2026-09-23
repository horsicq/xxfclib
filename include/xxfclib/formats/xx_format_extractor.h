/* Copyright (c) 2026 hors<horsicq@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file xx_format_extractor.h
 * @brief Streaming search for formats inside a device.
 *
 * A search walks a device and stops at each format it finds, the same way
 * an archive-record read walks the members of one archive:
 *
 *   state = ex->create_format_search(ex, device, options, pd);
 *   for (info = ex->get_current_format_info(ex, state); info;
 *        info = ex->format_search_find_next(ex, state, pd)
 *                   ? ex->get_current_format_info(ex, state) : NULL) {
 *       ... info->file_type, info->offset, info->size ...
 *   }
 *   ex->free_format_search(ex, state);
 */

#ifndef XX_FORMAT_EXTRACTOR_H
#define XX_FORMAT_EXTRACTOR_H

#include "xxfclib/xxfc_defs.h"
#include "xxfclib/io/xx_io.h"
#include "xxfclib/data/xx_pd.h"
#include "xxfclib/list/xx_list.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct xx_format_extractor xx_format_extractor;
typedef struct xx_format_extractor xx_format_extractor_t;

/** Search in progress. Opaque: defined and owned by the implementation. */
typedef struct xx_format_search_state xx_format_search_state;
typedef struct xx_format_search_state xx_format_search_state_t;

typedef struct xx_format_search_info xx_format_search_info;
typedef struct xx_format_search_info xx_format_search_info_t;

/**
 * @brief A format the search has found.
 */
struct xx_format_search_info {
  xx_file_type_t file_type; /**< What was found */
  int64_t offset;           /**< Where it starts in the searched device */
  int64_t size;             /**< Its size in bytes, or -1 if not known */
};

/**
 * @brief Streaming format search callbacks.
 */
struct xx_format_extractor {
  /**
   * Start a search over @p device, positioned on the first find.
   * The device stays the caller's and must outlive the search.
   * @return NULL on failure.
   */
  xx_format_search_state *(*create_format_search)(xx_format_extractor *self, xx_io_device *device, const xx_list_s *options, xx_pd_struct *pd);

  /**
   * The find the search is positioned on.
   * @return NULL when the search found nothing, or has moved past the last
   *         find. The info is owned by @p state and stays valid until the
   *         next find_next or free call.
   */
  const xx_format_search_info *(*get_current_format_info)(xx_format_extractor *self, xx_format_search_state *state);

  /**
   * Move to the next find.
   * @return false when there are no more finds, or @p pd stopped the search.
   */
  bool (*format_search_find_next)(xx_format_extractor *self, xx_format_search_state *state, xx_pd_struct *pd);

  /** Close the search and free its state. A NULL @p state is ignored. */
  void (*free_format_search)(xx_format_extractor *self, xx_format_search_state *state);
};

/* --- One extractor per format --- */

/**
 * The search for the format that @p type belongs to.
 * @return NULL when no compiled reader answers to @p type.
 */
XXFC_API xx_format_extractor *xx_format_extractor_get(xx_file_type_t type);

/** Number of file types that have an extractor. */
XXFC_API size_t xx_format_extractor_count(void);

/**
 * Enumerate the extractors: @p index runs from 0 to xx_format_extractor_count() - 1.
 * Formats that answer to several types (ZIP and ZIP64, ELF32 and ELF64, ...)
 * appear once per type, with the same extractor.
 * @param type Receives the file type of this entry; may be NULL.
 * @return NULL when @p index is out of range.
 */
XXFC_API xx_format_extractor *xx_format_extractor_at(size_t index, xx_file_type_t *type);

#ifdef __cplusplus
}
#endif

#endif /* XX_FORMAT_EXTRACTOR_H */
