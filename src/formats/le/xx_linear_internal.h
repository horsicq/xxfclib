/* Copyright (c) 2026 hors<horsicq@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#ifndef XXFCLIB_LINEAR_INTERNAL_H
#define XXFCLIB_LINEAR_INTERNAL_H

#include "xxfclib/formats/le/xx_le.h"

void xx_linear_init_impl(xx_linear_executable *linear,
                         xx_io_device *device, int64_t base_address,
                         uint16_t signature);
void xx_linear_destroy_impl(xx_linear_executable *linear);
bool xx_linear_check_impl(Abstractformat *format, xx_pd_struct *pd,
                          uint16_t signature);
bool xx_linear_handle_impl(Abstractformat *format, xx_pd_struct *pd,
                           uint16_t signature);
int64_t xx_linear_get_format_size_impl(Abstractformat *format,
                                       xx_pd_struct *pd,
                                       uint16_t signature);
uint64_t xx_linear_get_import_count_impl(Abstractformat *format,
                                         xx_pd_struct *pd,
                                         uint16_t signature);
uint64_t xx_linear_get_export_count_impl(Abstractformat *format,
                                         xx_pd_struct *pd,
                                         uint16_t signature);
uint64_t xx_linear_get_resource_count_impl(Abstractformat *format,
                                           xx_pd_struct *pd,
                                           uint16_t signature);
bool xx_linear_get_memory_map_impl(Abstractformat *format,
                                   xx_memory_map_mode_t mode,
                                   xx_memory_map *output,
                                   xx_pd_struct *pd,
                                   uint16_t signature);

#endif /* XXFCLIB_LINEAR_INTERNAL_H */
