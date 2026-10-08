/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

/* Internal adapter from a format search descriptor to Abstractextractor.
 * Define one adapter in each format's extractor translation unit.
 */

#ifndef XX_FORMAT_ABSTRACT_EXTRACTOR_ADAPTER_H
#define XX_FORMAT_ABSTRACT_EXTRACTOR_ADAPTER_H

#include "xx_format_extractor_engine.h"

#define XX_FORMAT_DEFINE_ABSTRACT_EXTRACTOR(name, descriptor)                       \
  bool xx_##name##_fast_detect(xx_io_device *device, int64_t base_address,           \
                               bool is_mapped) {                                    \
    return xx_format_search_fast_detect(&(descriptor), device, base_address,         \
                                        is_mapped);                                  \
  }                                                                                  \
  int64_t xx_##name##_size(xx_io_device *device, int64_t base_address,               \
                           bool is_mapped) {                                        \
    return xx_format_search_size(&(descriptor), device, base_address, is_mapped);     \
  }                                                                                  \
  static xx_format_search_state *xx_##name##_abstract_create_format_search(          \
      Abstractextractor *self, xx_io_device *device, const xx_list_s *options,       \
      xx_pd_struct *pd) {                                                            \
    (void)self;                                                                      \
    return xx_format_search_create(&(descriptor), device, options, pd);              \
  }                                                                                  \
  static const xx_format_search_info *xx_##name##_abstract_get_current_format_info( \
      Abstractextractor *self, xx_format_search_state *state) {                      \
    (void)self;                                                                      \
    return xx_format_search_current(state);                                         \
  }                                                                                  \
  static bool xx_##name##_abstract_format_search_find_next(                         \
      Abstractextractor *self, xx_format_search_state *state, xx_pd_struct *pd) {    \
    (void)self;                                                                      \
    return xx_format_search_find_next(state, pd);                                   \
  }                                                                                  \
  static void xx_##name##_abstract_free_format_search(                              \
      Abstractextractor *self, xx_format_search_state *state) {                      \
    (void)self;                                                                      \
    xx_format_search_free(state);                                                    \
  }                                                                                  \
  static Abstractextractor xx_##name##_abstract_extractor = {                       \
      .fast_detect = xx_##name##_fast_detect,                                         \
      .size = xx_##name##_size,                                                       \
      .create_format_search = xx_##name##_abstract_create_format_search,             \
      .get_current_format_info = xx_##name##_abstract_get_current_format_info,       \
      .format_search_find_next = xx_##name##_abstract_format_search_find_next,       \
      .free_format_search = xx_##name##_abstract_free_format_search};                \
  Abstractextractor *xx_##name##_get_abstract_extractor(void) {                      \
    return &xx_##name##_abstract_extractor;                                          \
  }

#endif /* XX_FORMAT_ABSTRACT_EXTRACTOR_ADAPTER_H */
